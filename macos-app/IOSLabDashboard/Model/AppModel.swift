import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if canImport(Combine)
import Combine
#endif
#if canImport(IOSLabDashboardCore)
import IOSLabDashboardCore
#endif

/// A short message shown at the bottom of the window. Failed actions always produce one.
struct Toast: Identifiable, Equatable {
    let id = UUID()
    var message: String
    var isError: Bool
}

/// What a sheet is open for.
enum AppSheet: Identifiable, Equatable {
    case settings
    case newSimulator
    /// `nil` creates a scheme, an id edits one.
    case scheme(String?)
    case token
    case shortcuts
    case openQuickly

    var id: String {
        switch self {
        case .settings: return "settings"
        case .newSimulator: return "newSimulator"
        case .scheme(let id): return "scheme:\(id ?? "new")"
        case .token: return "token"
        case .shortcuts: return "shortcuts"
        case .openQuickly: return "openQuickly"
        }
    }
}

/// The one observable object of the window. It owns the network side (API client, event streams, polling) and
/// the UI state (selection, panels), and delegates every decision that does not need the network or SwiftUI to
/// `IOSLabDashboardCore`: the reducer, the trees, the toolbar strings, the run planner.
@MainActor
final class AppModel: ObservableObject {
    // MARK: Published state

    /// Everything that came from the backend.
    @Published private(set) var state = DashboardState()
    /// Panel sizes, appearance, schemes: persisted on every change.
    @Published var ui: UIState {
        didSet { if ui != oldValue { uiStore.save(ui) } }
    }
    @Published private(set) var runtimeStatus: RuntimeStatus = .idle
    @Published private(set) var runtimeNote = ""
    @Published private(set) var baseURL: URL?

    @Published private(set) var target: EditorTarget = .welcome
    @Published var reportTab: ReportTab = .summary
    @Published private(set) var log = JobLogState()
    @Published private(set) var artifacts: [String: [Artifact]] = [:]
    /// Which line of the open log to scroll to (an issue's location), and a counter so the same line can be requested again.
    @Published private(set) var logFocusLine: Int?
    @Published private(set) var logFocusSerial = 0

    @Published var filters: [NavigatorTab: NavigatorFilter] = [:]
    @Published var expanded: [NavigatorTab: Set<String>] = [:]
    @Published var selectedNodeID: String?
    @Published var findQuery = ""
    @Published var consoleQuery = ""
    @Published var variablesQuery = ""

    @Published var sheet: AppSheet?
    @Published private(set) var toasts: [Toast] = []
    /// The last failure of a background load (shown inline in the navigators, in the fail colour).
    @Published private(set) var loadError: String?
    @Published private(set) var busy: Set<String> = []

    @Published private(set) var doctor: DoctorReport?
    @Published private(set) var doctorError: String?
    @Published private(set) var doctorRunning = false
    @Published private(set) var backStack: [EditorTarget] = []
    @Published private(set) var forwardStack: [EditorTarget] = []

    /// Clearing the console hides what is there now; new output appears again.
    @Published private(set) var consoleFloor: [String: Int] = [:]
    @Published private(set) var activityClearedAt: Date?

    /// Set by the window so the model can ask for focus on a navigator filter field (Cmd Shift F).
    @Published private(set) var focusFindSerial = 0

    // MARK: Dependencies

    private let uiStore: UIStateStore
    private let tokenStore: TokenStore
    private let runtime: BackendRuntime
    private let connector: WebSocketConnector
    let formatting: DateFormatting

    private(set) var client: APIClient?

    // MARK: Tasks (each holds `self` weakly, so the model never keeps itself alive through its own loops)

    private var bootstrapTask: Task<Void, Never>?
    private var globalStream: Task<Void, Never>?
    private var pollTask: Task<Void, Never>?
    private var jobStream: Task<Void, Never>?
    private var logTask: Task<Void, Never>?
    private var resultsInFlight: Set<String> = []
    private var finalFetchedJobID: String?
    private var pending: RefreshRequest = []
    private var pendingSince: Date?
    private var knownExpandable: [NavigatorTab: Set<String>] = [:]
    private var refreshDebouncer: Debouncer?

    init(uiStore: UIStateStore, tokenStore: TokenStore, runtime: BackendRuntime, connector: WebSocketConnector) {
        self.uiStore = uiStore
        self.tokenStore = tokenStore
        self.runtime = runtime
        self.connector = connector
        self.ui = uiStore.load()
        let hourFormat = DateFormatter.dateFormat(fromTemplate: "j", options: 0, locale: Locale.current) ?? "h a"
        self.formatting = DateFormatting(calendar: .current, use24Hour: !hourFormat.contains("a"))
        self.refreshDebouncer = Debouncer(delay: 0.3) { [weak self] in
            await self?.flushPending()
        }
        runtime.onExit = { [weak self] code, output in
            Task { @MainActor in self?.backendExited(code: code, output: output) }
        }
    }

    // MARK: - Lifecycle

    /// Starts (or attaches to) the backend and connects. Called once when the window appears.
    func start() {
        guard bootstrapTask == nil else { return }
        bootstrapTask = Task { [weak self] in
            await self?.bootstrap()
        }
    }

    /// Stops the backend this app started. Called when the app quits.
    func shutdown() {
        stopNetworking()
        runtime.stop()
    }

    /// Connects again, for example after the backend address or token changed in Settings.
    func reconnect() {
        bootstrapTask?.cancel()
        bootstrapTask = nil
        stopNetworking()
        runtime.stop()
        state = DashboardState()
        runtimeStatus = .idle
        start()
    }

    private var preferredURL: URL? {
        if let text = ui.apiURL, let url = APIConfiguration.parseBaseURL(text) { return url }
        if let text = ProcessInfo.processInfo.environment["IOSLAB_BACKEND_URL"], let url = APIConfiguration.parseBaseURL(text) { return url }
        return nil
    }

    private func bootstrap() async {
        runtimeStatus = .idle
        let outcome = await runtime.start(preferredURL: preferredURL)
        if Task.isCancelled { return }
        runtimeStatus = outcome.status
        runtimeNote = outcome.note
        if let url = outcome.url {
            connect(to: url)
        } else {
            apply(.connection(.disconnected(reason: outcome.note)))
        }
    }

    private func backendExited(code: Int32, output: String) {
        runtimeStatus = .exited(code: code, tail: output)
        runtimeNote = runtimeStatus.message
        apply(.connection(.disconnected(reason: runtimeNote)))
    }

    private func connect(to url: URL) {
        stopNetworking()
        baseURL = url
        let token = tokenStore.token(for: url.absoluteString)
        let api = APIClient(configuration: APIConfiguration(baseURL: url, token: token))
        client = api

        let events = EventStreamClient(api: api, connector: connector)
        globalStream = Task { [weak self] in
            for await signal in events.signals(EventStreamOptions(replay: 100)) {
                guard let self else { return }
                self.handle(signal)
            }
        }

        pollTask = Task { [weak self] in
            await self?.initialLoad(api)
            var sinceList = 0.0
            var sinceMetrics = 100.0
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                if Task.isCancelled { return }
                sinceList += 1
                sinceMetrics += 1
                guard let self else { return }
                if sinceList >= PollingPolicy.listInterval(connection: self.state.connection) {
                    sinceList = 0
                    await self.refresh(.all, api: api)
                }
                if let interval = PollingPolicy.metricsInterval(debugNavigatorVisible: self.debugNavigatorVisible), sinceMetrics >= interval {
                    sinceMetrics = 0
                    await self.refresh(.metrics, api: api)
                }
            }
        }
    }

    private func stopNetworking() {
        globalStream?.cancel()
        pollTask?.cancel()
        jobStream?.cancel()
        logTask?.cancel()
        globalStream = nil
        pollTask = nil
        jobStream = nil
        logTask = nil
        client = nil
        resultsInFlight = []
        Task { await refreshDebouncer?.cancel() }
        pending = []
    }

    private var debugNavigatorVisible: Bool { ui.navigatorVisible && ui.navigatorTab == .debug }

    // MARK: - Loading

    nonisolated static func fetch<T: Sendable>(_ operation: @Sendable () async throws -> T) async -> Result<T, Error> {
        do { return .success(try await operation()) } catch { return .failure(error) }
    }

    private func initialLoad(_ api: APIClient) async {
        async let health = Self.fetch { try await api.health() }
        async let capabilities = Self.fetch { try await api.capabilities() }
        async let catalog = Self.fetch { try await api.catalog() }
        let (h, c, k) = await (health, capabilities, catalog)
        deliver(h, api: api) { .health($0) }
        deliver(c, api: api) { .capabilities($0) }
        deliver(k, api: api) { .catalog($0) }
        await refresh([.all, .metrics], api: api)
    }

    /// Fetches what `request` names, concurrently, and feeds the answers to the reducer.
    func refresh(_ request: RefreshRequest, api: APIClient? = nil) async {
        guard let api = api ?? client else { return }
        async let devices = request.contains(.devices) ? Self.fetch { try await api.devices() } : nil
        async let jobs = request.contains(.jobs) ? Self.fetch { try await api.jobs(limit: 200) } : nil
        async let runs = request.contains(.runs) ? Self.fetch { try await api.runs() } : nil
        async let metrics = request.contains(.metrics) ? Self.fetch { try await api.metrics() } : nil
        let (d, j, r, m) = await (devices, jobs, runs, metrics)
        if let d { deliver(d, api: api) { .devices($0) } }
        if let j { deliver(j, api: api) { .jobs($0) } }
        if let r { deliver(r, api: api) { .runs($0) } }
        if let m { deliver(m, api: api) { .metrics($0) } }
        if j != nil { jobsChanged() }
    }

    /// The button in the toolbar menu and the Refresh command.
    func refreshNow() {
        Task { await refresh([.all, .metrics]) }
    }

    private func deliver<T>(_ result: Result<T, Error>, api: APIClient, _ action: (T) -> DashboardAction) {
        guard client === api else { return }
        switch result {
        case .success(let value):
            loadError = nil
            apply(action(value))
        case .failure(let error):
            noteFailure(error)
        }
    }

    private func noteFailure(_ error: Error) {
        if let api = error as? APIError {
            if api.isUnauthorized {
                loadError = api.message
                if sheet == nil { sheet = .token }
            } else if !api.isConnectivity {
                loadError = api.message
            }
        } else if !(error is CancellationError) {
            loadError = error.localizedDescription
        }
    }

    // MARK: - Event stream

    private func handle(_ signal: StreamSignal) {
        switch signal {
        case .connecting:
            break
        case .connected:
            apply(.connection(.connected))
        case .event(let event):
            apply(.event(event))
        case .malformed:
            break
        case .disconnected(let reason, _):
            apply(.connection(.disconnected(reason: reason)))
        }
    }

    /// Runs one action through the reducer and schedules the refetch it asks for.
    func apply(_ action: DashboardAction) {
        let request = state.reduce(action)
        if !request.isEmpty { schedule(request) }
        switch action {
        case .devices, .jobs, .runs, .results, .upsertJob, .removeDevice, .upsertDevice:
            refreshExpansion()
        default:
            break
        }
    }

    private func schedule(_ request: RefreshRequest) {
        // Immediately on reconnect; otherwise coalesce bursts, but never wait longer than 1.5 s in a steady stream.
        pending.formUnion(request)
        if pendingSince == nil { pendingSince = Date() }
        if let since = pendingSince, Date().timeIntervalSince(since) > 1.5 {
            Task { await flushPending() }
            return
        }
        let debouncer = refreshDebouncer
        Task { await debouncer?.trigger() }
    }

    private func flushPending() async {
        let request = pending
        pending = []
        pendingSince = nil
        if request.isEmpty { return }
        await refresh(request)
    }

    /// Runs after every jobs refresh: loads what the open job and the Issues navigator need.
    private func jobsChanged() {
        if let jobID = target.jobID, let job = state.job(id: jobID), job.status.isTerminal, finalFetchedJobID != jobID {
            finalFetchedJobID = jobID
            jobStream?.cancel()
            jobStream = nil
            fetchFinalOutput(jobID: jobID)
            loadResults(jobID: jobID, force: true)
            loadArtifacts(jobID: jobID)
        }
        // Failed tests are listed in the Issues navigator, which needs each failed job's results.
        for job in state.jobs.filter({ $0.status == .failed }).prefix(15) where state.results[job.id] == nil {
            loadResults(jobID: job.id)
        }
    }

    // MARK: - Results, logs, artifacts

    func loadResults(jobID: String, force: Bool = false) {
        guard let api = client, force || state.results[jobID] == nil, !resultsInFlight.contains(jobID) else { return }
        resultsInFlight.insert(jobID)
        Task { [weak self] in
            let result = await Self.fetch { try await api.results(jobID: jobID) }
            guard let self else { return }
            self.resultsInFlight.remove(jobID)
            if case .success(let value) = result, self.client === api { self.apply(.results(jobID: jobID, value)) }
        }
    }

    func loadArtifacts(jobID: String) {
        guard let api = client else { return }
        Task { [weak self] in
            let result = await Self.fetch { try await api.artifacts(jobID: jobID) }
            guard let self else { return }
            if case .success(let value) = result, self.client === api { self.artifacts[jobID] = value }
        }
    }

    private func fetchFinalOutput(jobID: String) {
        guard let api = client else { return }
        Task { [weak self] in
            let result = await Self.fetch { try await api.output(jobID: jobID) }
            guard let self, self.log.jobID == jobID else { return }
            if case .success(let value) = result { self.log.replaceWithFinal(value) }
        }
    }

    /// Points the log at a job: subscribes to its live output first, then fetches the snapshot and merges.
    private func beginLog(for jobID: String) {
        jobStream?.cancel()
        jobStream = nil
        logTask?.cancel()
        log.reset(jobID: jobID)
        finalFetchedJobID = nil
        guard let api = client else {
            log.failed("Not connected to the backend.")
            return
        }
        let job = state.job(id: jobID)
        let active = job.map { $0.status.isActive } ?? false
        if job?.status.isTerminal == true { finalFetchedJobID = jobID }

        if active {
            let stream = EventStreamClient(api: api, connector: connector)
            jobStream = Task { [weak self] in
                for await signal in stream.signals(EventStreamOptions(jobID: jobID, includeOutput: true, replay: 0)) {
                    guard let self else { return }
                    if case .event(let event) = signal, self.log.jobID == jobID { self.log.appendLive(event) }
                }
            }
        }
        logTask = Task { [weak self] in
            let result = await Self.fetch { try await api.output(jobID: jobID) }
            guard let self, self.log.jobID == jobID, !Task.isCancelled else { return }
            switch result {
            case .success(let output): self.log.applySnapshot(output)
            case .failure(let error): self.log.failed((error as? APIError)?.message ?? error.localizedDescription)
            }
            self.applyFocusIfNeeded()
        }
        loadResults(jobID: jobID, force: true)
        loadArtifacts(jobID: jobID)
    }

    private var pendingFocus: String?

    private func applyFocusIfNeeded() {
        guard let needle = pendingFocus else { return }
        pendingFocus = nil
        if let line = log.document.lineNumber(containing: needle) {
            logFocusLine = line
            logFocusSerial += 1
        }
    }

    func scrollLog(to line: Int) {
        logFocusLine = line
        logFocusSerial += 1
    }

    // MARK: - Selection and history

    /// Opens something in the editor. `focus` is text to find in the job's log (an issue's `File.swift:42`).
    func open(_ newTarget: EditorTarget, tab: ReportTab? = nil, focus: String? = nil, record: Bool = true) {
        if let tab { reportTab = tab }
        if newTarget != target {
            if record {
                backStack.append(target)
                if backStack.count > 100 { backStack.removeFirst() }
                forwardStack.removeAll()
            }
            target = newTarget
            if let jobID = newTarget.jobID {
                pendingFocus = focus
                beginLog(for: jobID)
            } else {
                jobStream?.cancel()
                jobStream = nil
                log.reset(jobID: nil)
            }
        } else if let jobID = newTarget.jobID, let focus {
            pendingFocus = focus
            if log.jobID == jobID, !log.isEmpty { applyFocusIfNeeded() } else if log.jobID != jobID { beginLog(for: jobID) }
        }
        if newTarget.deviceID != nil, ui.inspectorVisible == false { /* the inspector stays as the person left it */ }
    }

    func open(node: TreeNode) {
        guard let target = node.target else { return }
        open(target, tab: node.tab, focus: node.focus)
    }

    var canGoBack: Bool { !backStack.isEmpty }
    var canGoForward: Bool { !forwardStack.isEmpty }

    func goBack() {
        guard let previous = backStack.popLast() else { return }
        forwardStack.append(target)
        open(previous, record: false)
    }

    func goForward() {
        guard let next = forwardStack.popLast() else { return }
        backStack.append(target)
        open(next, record: false)
    }

    // MARK: - Panels

    func toggleNavigator() { ui.navigatorVisible.toggle() }
    func toggleInspector() { ui.inspectorVisible.toggle() }
    func toggleDebugArea() { ui.debugVisible.toggle() }

    func select(tab: NavigatorTab) {
        ui.navigatorTab = tab
        ui.navigatorVisible = true
    }

    func focusFind() {
        select(tab: .find)
        focusFindSerial += 1
    }

    // MARK: - Trees

    var catalog: Catalog? { state.catalog }
    var vmEnabled: Bool { state.capabilities?.vm?.enabled ?? false }

    func filter(for tab: NavigatorTab) -> NavigatorFilter { filters[tab] ?? NavigatorFilter() }

    func setFilter(_ filter: NavigatorFilter, for tab: NavigatorTab) { filters[tab] = filter }

    func devicesTree() -> [TreeNode] {
        DevicesTree.build(devices: state.devices, vmEnabled: vmEnabled, filter: filter(for: .devices))
    }

    func testsTree() -> [TreeNode] {
        TestsTree.build(runs: state.runs, jobs: state.jobs, results: state.results, catalog: catalog, filter: filter(for: .tests))
    }

    func issuesTree(now: Date = Date()) -> [TreeNode] {
        IssuesTree.build(jobs: state.jobs, results: state.results, now: now, formatting: formatting, catalog: catalog, filter: filter(for: .issues))
    }

    func reportsTree(now: Date = Date()) -> [TreeNode] {
        ReportsTree.build(jobs: state.jobs, now: now, formatting: formatting, catalog: catalog, filter: filter(for: .reports))
    }

    var debugModel: DebugModel {
        DebugNavigator.build(metrics: state.metrics, capacity: state.capacity, jobs: state.jobs, catalog: catalog)
    }

    func variablesTree() -> [TreeNode] {
        guard let jobID = target.jobID else { return [] }
        return VariablesTree.build(
            job: state.job(id: jobID), results: state.results[jobID], runningTest: log.runningTestName, mode: ui.variablesMode, query: variablesQuery
        )
    }

    func findGroups() -> [FindGroup] {
        FindEngine.search(
            query: findQuery, scope: ui.findScope, state: state, log: log.document, openJobID: target.jobID, catalog: catalog, formatting: formatting
        )
    }

    func quickOpenItems(now: Date = Date()) -> [QuickOpenItem] {
        QuickOpen.index(state: state, formatting: formatting, now: now, catalog: catalog)
    }

    func inspectorSections(now: Date = Date()) -> [InspectorSection] {
        Inspector.sections(for: target, state: state, now: now, formatting: formatting, catalog: catalog)
    }

    func inspectorHistory() -> [(id: String, time: String, text: String, isError: Bool)] {
        let events: [EngineEvent]
        switch target {
        case .welcome: events = state.activity
        case .device(let id): events = state.activity.filter { $0.deviceId == id }
        case .job(let id): events = state.activity.filter { $0.jobId == id }
        case .run(let id): events = state.activity.filter { $0.runId == id }
        }
        return Inspector.history(events: events, formatting: formatting)
    }

    func quickHelp() -> String { Inspector.quickHelp(for: target, state: state) }

    func breadcrumbs(now: Date = Date()) -> [Crumb] {
        Breadcrumbs.build(target: target, tab: reportTab, state: state, now: now, formatting: formatting, catalog: catalog)
    }

    func capsuleStatus(now: Date = Date()) -> CapsuleStatus {
        CapsuleStatus.make(scheme: currentScheme?.name, state: state, now: now, formatting: formatting)
    }

    /// Keeps the expanded sets sensible: new runtime folders and issue groups open by themselves, the `Runs` root too.
    private func refreshExpansion() {
        func track(_ tab: NavigatorTab, _ roots: [TreeNode], auto: (Set<String>) -> Set<String>) {
            let ids = TreeFlattener.expandableIDs(roots)
            let fresh = ids.subtracting(knownExpandable[tab] ?? [])
            guard !fresh.isEmpty else { return }
            knownExpandable[tab, default: []].formUnion(fresh)
            expanded[tab, default: []].formUnion(auto(fresh))
        }
        let unfiltered = NavigatorFilter()
        track(.devices, DevicesTree.build(devices: state.devices, vmEnabled: vmEnabled, filter: unfiltered)) { $0 }
        track(.issues, IssuesTree.build(jobs: state.jobs, results: state.results, now: Date(), formatting: formatting, catalog: catalog)) { $0 }
        track(.tests, TestsTree.build(runs: state.runs, jobs: state.jobs, results: state.results, catalog: catalog)) { $0.intersection([TestsTree.rootID]) }
    }

    func setExpanded(_ ids: Set<String>, for tab: NavigatorTab) { expanded[tab] = ids }

    // MARK: - Schemes and destinations

    var schemes: [SchemeConfig] { SchemeList.merged(stored: ui.schemes, jobs: state.jobs) }

    var currentScheme: SchemeConfig? { SchemeList.selected(id: ui.selectedSchemeID, in: schemes, jobs: state.jobs) }

    func selectScheme(_ scheme: SchemeConfig) { ui.selectedSchemeID = scheme.id }

    /// Saves a scheme (new or edited) and selects it. A scheme that only existed as a name from the history becomes a real one.
    func save(scheme: SchemeConfig) {
        var stored = ui.schemes
        if let index = stored.firstIndex(where: { $0.id == scheme.id }) {
            stored[index] = scheme
        } else if let index = stored.firstIndex(where: { $0.name == scheme.name }) {
            var replaced = scheme
            replaced.id = stored[index].id
            stored[index] = replaced
        } else {
            stored.append(scheme)
        }
        ui.schemes = stored
        ui.selectedSchemeID = scheme.isRecent ? nil : scheme.id
        if let saved = ui.schemes.first(where: { $0.name == scheme.name }) { ui.selectedSchemeID = saved.id }
    }

    func deleteScheme(id: String) {
        ui.schemes.removeAll { $0.id == id }
        if ui.selectedSchemeID == id { ui.selectedSchemeID = nil }
    }

    var destinationMenu: DestinationMenu { Destinations.menu(devices: state.devices, catalog: catalog) }

    var destinationSummary: String {
        Destinations.summary(selected: ui.destinations, devices: state.devices, catalog: catalog)
    }

    func isSelected(_ key: DestinationKey) -> Bool {
        key.isAny ? ui.destinations.filter { !$0.isAny }.isEmpty : ui.destinations.contains(key)
    }

    /// Ticks or unticks a destination; "Any Available Simulator" clears the list.
    func toggle(destination key: DestinationKey, exclusive: Bool = true) {
        if key.isAny {
            ui.destinations = []
        } else if exclusive {
            ui.destinations = [key]
        } else if let index = ui.destinations.firstIndex(of: key) {
            ui.destinations.remove(at: index)
        } else {
            ui.destinations.append(key)
        }
    }

    // MARK: - Actions

    var canRun: Bool { client != nil && state.connection.isConnected }
    var canStop: Bool { state.hasActiveWork }

    func run(scheme override: SchemeConfig? = nil, destinations: [DestinationKey]? = nil) {
        guard let api = client else {
            toast("Not connected to the backend.", isError: true)
            return
        }
        guard let scheme = override ?? currentScheme else {
            toast("Create a scheme first: it names the Xcode scheme to test.", isError: false)
            sheet = .scheme(nil)
            return
        }
        let problems = scheme.problems()
        if let first = problems.first {
            toast(first, isError: true)
            sheet = .scheme(scheme.id)
            return
        }
        let plans = RunPlanner.plan(scheme: scheme, destinations: destinations ?? ui.destinations)
        Task { [weak self] in
            var opened: EditorTarget?
            for plan in plans {
                do {
                    switch plan {
                    case .single(let request):
                        let job = try await api.runTest(request)
                        guard let self else { return }
                        self.apply(.upsertJob(job))
                        if opened == nil { opened = .job(job.id) }
                    case .matrix(let request):
                        let response = try await api.createRun(request)
                        guard let self else { return }
                        for job in response.jobs { self.apply(.upsertJob(job)) }
                        if opened == nil { opened = .run(response.run.id) }
                        for skipped in response.skipped { self.toast("Skipped \(skipped.model) on \(skipped.runtime): \(skipped.reason)", isError: false) }
                    }
                } catch {
                    self?.report(error)
                }
            }
            if let self {
                if let opened { self.open(opened, tab: .summary) }
                await self.refresh([.jobs, .runs, .devices])
            }
        }
    }

    /// Cancels what is queued or running: the selected scheme's jobs first, else everything.
    func stop() {
        guard let api = client else { return }
        let name = currentScheme?.name
        let same = state.activeJobs.filter { name == nil || $0.testTarget == name }
        let targets = same.isEmpty ? state.activeJobs : same
        Task { [weak self] in
            for job in targets {
                do {
                    let updated = try await api.cancel(jobID: job.id)
                    self?.apply(.upsertJob(updated))
                } catch {
                    self?.report(error)
                }
            }
            await self?.refresh([.jobs, .runs, .devices])
        }
    }

    func cancel(jobID: String) {
        guard let api = client else { return }
        perform("job:\(jobID)") {
            let job = try await api.cancel(jobID: jobID)
            self.apply(.upsertJob(job))
        }
    }

    func rerun(jobID: String) {
        guard let api = client else { return }
        perform("job:\(jobID)") {
            let job = try await api.rerun(jobID: jobID)
            self.apply(.upsertJob(job))
            self.open(.job(job.id), tab: .summary)
            self.toast("Started \(JobText.title(job, catalog: self.catalog)).", isError: false)
        }
    }

    func rerunFailed(runID: String) {
        let failed = state.jobs.filter { $0.runId == runID && $0.status == .failed }
        guard !failed.isEmpty else { toast("No failed jobs in this run.", isError: false); return }
        for job in failed { rerun(jobID: job.id) }
    }

    func cancel(runID: String) {
        guard let api = client else { return }
        perform("run:\(runID)") {
            _ = try await api.cancel(runID: runID)
            await self.refresh([.jobs, .runs, .devices])
        }
    }

    func boot(deviceID: String) {
        guard let api = client else { return }
        perform("device:\(deviceID)") {
            let device = try await api.bootDevice(id: deviceID)
            self.apply(.upsertDevice(device))
            await self.refresh(.devices)
        }
    }

    func shutdown(deviceID: String) {
        guard let api = client else { return }
        perform("device:\(deviceID)") {
            let device = try await api.shutdownDevice(id: deviceID)
            self.apply(.upsertDevice(device))
            await self.refresh(.devices)
        }
    }

    func delete(deviceID: String) {
        guard let api = client else { return }
        perform("device:\(deviceID)") {
            try await api.deleteDevice(id: deviceID)
            self.apply(.removeDevice(deviceID))
            if self.target == .device(deviceID) { self.open(.welcome, record: false) }
            await self.refresh(.devices)
        }
    }

    func spawn(_ request: SpawnDeviceRequest, completion: @escaping (Bool) -> Void = { _ in }) {
        guard let api = client else {
            toast("Not connected to the backend.", isError: true)
            completion(false)
            return
        }
        perform("spawn") {
            let device = try await api.spawnDevice(request)
            self.apply(.upsertDevice(device))
            self.open(.device(device.id))
            self.toast("Created \(device.name).", isError: false)
            await self.refresh(.devices)
        } completion: { completion($0) }
    }

    func syncDevices() {
        guard let api = client else { return }
        perform("sync") {
            let result = try await api.syncDevices()
            self.toast("Synced simulators: \(result.updated) updated, \(result.removed) removed.", isError: false)
            await self.refresh(.devices)
        }
    }

    func screenshot(deviceID: String) async throws -> Data {
        guard let api = client else { throw APIError.unreachable("Not connected to the backend.") }
        return try await api.screenshot(deviceID: deviceID)
    }

    func junit(jobID: String) async throws -> String {
        guard let api = client else { throw APIError.unreachable("Not connected to the backend.") }
        return try await api.junit(jobID: jobID)
    }

    func downloadURL(_ artifact: Artifact) -> URL? { client?.downloadURL(for: artifact) }

    func runDoctor() {
        guard let api = client, !doctorRunning else { return }
        doctorRunning = true
        doctorError = nil
        Task { [weak self] in
            let result = await Self.fetch { try await api.doctor() }
            guard let self else { return }
            self.doctorRunning = false
            switch result {
            case .success(let report): self.doctor = report
            case .failure(let error): self.doctorError = (error as? APIError)?.message ?? error.localizedDescription
            }
        }
    }

    func cleanup(olderThanDays days: Int?) {
        guard let api = client else { return }
        perform("cleanup") {
            let result = try await api.cleanup(days: days)
            self.toast("Removed \(result.jobsRemoved) jobs and \(result.artifactsRemoved) artifacts, freed \(Formatters.bytes(result.bytesFreed)).", isError: false)
            await self.refresh([.jobs, .runs, .metrics])
        }
    }

    /// Runs an async API call, tracks it in `busy`, reports failures as a toast and a 401 as the token sheet.
    private func perform(_ key: String, _ work: @escaping @MainActor () async throws -> Void, completion: @escaping (Bool) -> Void = { _ in }) {
        busy.insert(key)
        Task { [weak self] in
            var ok = true
            do {
                try await work()
            } catch {
                ok = false
                self?.report(error)
            }
            self?.busy.remove(key)
            completion(ok)
        }
    }

    func isBusy(_ key: String) -> Bool { busy.contains(key) }

    // MARK: - Token and address

    var storedToken: String { baseURL.flatMap { tokenStore.token(for: $0.absoluteString) } ?? "" }

    var authRequired: Bool { state.capabilities?.auth?.required ?? false }

    /// Saves the token for the current backend and connects again.
    func setToken(_ token: String) {
        guard let baseURL else { return }
        tokenStore.setToken(token, for: baseURL.absoluteString)
        sheet = nil
        loadError = nil
        connect(to: baseURL)
    }

    /// Saves a new backend address (empty means: the bundled backend) and connects again.
    func setAPIURL(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        ui.apiURL = trimmed.isEmpty ? nil : trimmed
        reconnect()
    }

    // MARK: - Console

    func clearConsole() {
        if let jobID = target.jobID {
            consoleFloor[jobID] = log.document.count
        } else {
            activityClearedAt = Date()
        }
    }

    var visibleConsoleFloor: Int { target.jobID.flatMap { consoleFloor[$0] } ?? 0 }

    /// Lines of the open job's log for the console, after Clear and the popup and text filters.
    func consoleLines() -> [LogLine] {
        guard target.jobID != nil else { return [] }
        let floor = visibleConsoleFloor
        return log.document.filtered(ui.consoleFilter, query: consoleQuery).filter { $0.number > floor }
    }

    /// The global activity log (shown when no job is open), oldest first.
    func activityLines() -> [EngineEvent] {
        var events = state.activity
        if let cleared = activityClearedAt { events = events.filter { $0.timestamp > cleared } }
        let needle = consoleQuery.trimmingCharacters(in: .whitespaces)
        if ui.consoleFilter == .errors { events = events.filter { $0.isError } }
        if !needle.isEmpty { events = events.filter { $0.message.range(of: needle, options: .caseInsensitive) != nil } }
        return events
    }

    // MARK: - Toasts and errors

    func toast(_ message: String, isError: Bool) {
        let item = Toast(message: message, isError: isError)
        toasts.append(item)
        if toasts.count > 4 { toasts.removeFirst() }
        Task { [weak self] in
            try? await Task.sleep(nanoseconds: isError ? 6_000_000_000 : 3_500_000_000)
            self?.dismiss(item.id)
        }
    }

    func dismiss(_ id: UUID) { toasts.removeAll { $0.id == id } }

    func report(_ error: Error) {
        if let api = error as? APIError, api.isUnauthorized {
            sheet = .token
            return
        }
        if error is CancellationError { return }
        toast((error as? APIError)?.message ?? error.localizedDescription, isError: true)
    }

    // MARK: - Commands

    func perform(_ command: AppCommand) {
        switch command {
        case .run: run()
        case .stop: stop()
        case .toggleNavigator: toggleNavigator()
        case .toggleInspector: toggleInspector()
        case .toggleDebugArea: toggleDebugArea()
        case .showDevices, .showTests, .showIssues, .showFind, .showDebug, .showReports:
            if let tab = ShortcutTable.tab(for: command) { select(tab: tab) }
        case .openQuickly: sheet = .openQuickly
        case .newSimulator: sheet = .newSimulator
        case .clearConsole: clearConsole()
        case .settings: sheet = .settings
        case .findInNavigator: focusFind()
        case .refresh: refreshNow()
        case .shortcuts: sheet = .shortcuts
        }
    }
}
