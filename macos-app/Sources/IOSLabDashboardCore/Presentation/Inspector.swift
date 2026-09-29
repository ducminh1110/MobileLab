import Foundation

public enum InspectorTab: String, CaseIterable, Codable, Sendable, Identifiable {
    case attributes
    case history
    case quickHelp

    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .attributes: return "Attributes"
        case .history: return "History"
        case .quickHelp: return "Quick Help"
        }
    }
    public var symbol: String {
        switch self {
        case .attributes: return "doc.text"
        case .history: return "clock"
        case .quickHelp: return "questionmark.circle"
        }
    }
}

public struct InspectorRow: Equatable, Sendable, Identifiable {
    public var id: String
    public var label: String
    public var value: String
    /// JetBrains Mono for identifiers and paths.
    public var monospaced: Bool
    /// Shows a Copy button.
    public var copyable: Bool
    public var tone: StatusTone?
    /// Opens something when clicked (the job a device is busy with).
    public var link: EditorTarget?

    public init(_ label: String, _ value: String, monospaced: Bool = false, copyable: Bool = false, tone: StatusTone? = nil, link: EditorTarget? = nil) {
        self.id = label
        self.label = label
        self.value = value
        self.monospaced = monospaced
        self.copyable = copyable
        self.tone = tone
        self.link = link
    }
}

public struct InspectorSection: Equatable, Sendable, Identifiable {
    public var id: String { title }
    public var title: String
    public var rows: [InspectorRow]
}

public enum Inspector {
    /// The Attributes tab: sections of label/value rows for what is selected, or for the backend when nothing is.
    public static func sections(for target: EditorTarget, state: DashboardState, now: Date, formatting: DateFormatting, catalog: Catalog? = nil) -> [InspectorSection] {
        switch target {
        case .welcome: return backend(state)
        case .device(let id): return state.device(id: id).map { device($0, state: state) } ?? []
        case .job(let id): return state.job(id: id).map { job($0, state: state, now: now, formatting: formatting, catalog: catalog) } ?? []
        case .run(let id): return state.run(id: id).map { run($0, formatting: formatting, now: now) } ?? []
        }
    }

    // MARK: Device

    static func device(_ device: Device, state: DashboardState) -> [InspectorSection] {
        var identity = [
            InspectorRow("Name", device.name),
            InspectorRow("Runtime", device.runtimeDisplayName),
            InspectorRow("Device type", device.modelName ?? device.modelId ?? "Unknown"),
            InspectorRow("Backend", device.backend.rawValue, monospaced: true)
        ]
        if device.ephemeral { identity.append(InspectorRow("Created", "Automatically for a run")) }

        var sections = [InspectorSection(title: "Identity and Type", rows: identity)]

        if let udid = device.simulatorUdid {
            sections.append(InspectorSection(title: "Location", rows: [InspectorRow("Simulator UDID", udid, monospaced: true, copyable: true)]))
        }

        var resources = [InspectorRow("Cost units", "\(device.isVM ? 4 : 1)")]
        if let jobID = device.currentJobId {
            let title = state.job(id: jobID).map { JobText.title($0) } ?? Formatters.shortID(jobID)
            resources.append(InspectorRow("Current job", title, link: .job(jobID)))
        } else {
            resources.append(InspectorRow("Current job", "None"))
        }
        if device.isVM {
            if let cpu = device.cpu { resources.append(InspectorRow("CPU", "\(cpu) cores")) }
            if let memory = device.memory { resources.append(InspectorRow("Memory", "\(memory) GB")) }
            if let disk = device.disk { resources.append(InspectorRow("Disk", "\(disk) GB")) }
            if let screen = device.screen { resources.append(InspectorRow("Screen", screen, monospaced: true)) }
        }
        sections.append(InspectorSection(title: "Resources", rows: resources))

        var status = [InspectorRow("State", device.status.label, tone: tone(device.status))]
        if device.canRunTests == false { status.append(InspectorRow("Runs tests", "No")) }
        if let error = device.lastError, !error.isEmpty { status.append(InspectorRow("Last error", error, tone: .failure)) }
        sections.append(InspectorSection(title: "Status", rows: status))
        return sections
    }

    // MARK: Job

    static func job(_ job: TestJob, state: DashboardState, now: Date, formatting: DateFormatting, catalog: Catalog?) -> [InspectorSection] {
        var identity = [
            InspectorRow("ID", job.id, monospaced: true, copyable: true),
            InspectorRow("Scheme", job.testTarget)
        ]
        if let runID = job.runId {
            identity.append(InspectorRow("Run", state.run(id: runID)?.title ?? Formatters.shortID(runID), link: .run(runID)))
        }
        var sections = [InspectorSection(title: "Identity", rows: identity)]

        var destination: [InspectorRow] = []
        if let device = job.assignedDeviceName {
            destination.append(InspectorRow("Device", device, link: job.assignedDeviceId.map { .device($0) }))
        }
        if let runtime = job.requiredRuntime { destination.append(InspectorRow("Runtime", JobText.runtimeName(runtime, catalog: catalog))) }
        if let model = job.requiredModelId { destination.append(InspectorRow("Device type", JobText.modelName(model, catalog: catalog))) }
        if destination.isEmpty { destination.append(InspectorRow("Device", job.status == .queued ? "Not assigned yet" : "Unknown")) }
        sections.append(InspectorSection(title: "Destination", rows: destination))

        var execution = [
            InspectorRow("Status", statusLabel(job.status), tone: tone(job.status)),
            InspectorRow("Attempts", "\(job.attempts) of \(job.maxRetries + 1)"),
            InspectorRow("Retries", "\(job.retries) of \(job.maxRetries)")
        ]
        if let code = job.exitCode { execution.append(InspectorRow("Exit code", "\(code)", monospaced: true)) }
        if let seconds = job.elapsedSeconds(now: now) { execution.append(InspectorRow("Duration", Formatters.duration(seconds))) }
        if let started = job.startedAt { execution.append(InspectorRow("Started", formatting.dayAndTime(started, now: now))) }
        if let finished = job.finishedAt { execution.append(InspectorRow("Finished", formatting.dayAndTime(finished, now: now))) }
        if let reason = job.waitingReason, job.status == .queued || job.status == .retrying { execution.append(InspectorRow("Waiting", reason)) }
        sections.append(InspectorSection(title: "Execution", rows: execution))

        if let summary = job.summary {
            var result = [
                InspectorRow("Total", "\(summary.total)"),
                InspectorRow("Passed", "\(summary.passed)", tone: summary.passed > 0 ? .success : nil),
                InspectorRow("Failed", "\(summary.failed)", tone: summary.failed > 0 ? .failure : nil),
                InspectorRow("Skipped", "\(summary.skipped)")
            ]
            if summary.buildFailed { result.append(InspectorRow("Build", "Failed", tone: .failure)) }
            sections.append(InspectorSection(title: "Result", rows: result))
        }
        if let error = job.error, !error.isEmpty {
            sections.append(InspectorSection(title: "Error", rows: [InspectorRow("Message", error, tone: .failure)]))
        }
        return sections
    }

    // MARK: Run

    static func run(_ run: TestRunView, formatting: DateFormatting, now: Date) -> [InspectorSection] {
        var identity = [InspectorRow("ID", run.id, monospaced: true, copyable: true), InspectorRow("Scheme", run.scheme)]
        if let name = run.name, !name.isEmpty { identity.insert(InspectorRow("Name", name), at: 1) }
        var progress = [
            InspectorRow("Status", run.status.rawValue.capitalized, tone: tone(run.status)),
            InspectorRow("Jobs", "\(run.jobIds.count)"),
            InspectorRow("Summary", JobText.runCounts(run)),
            InspectorRow("Created", formatting.dayAndTime(run.createdAt, now: now))
        ]
        if let parallel = run.maxParallel { progress.append(InspectorRow("Max parallel", "\(parallel)")) }
        if let finished = run.finishedAt { progress.append(InspectorRow("Finished", formatting.dayAndTime(finished, now: now))) }
        return [InspectorSection(title: "Identity", rows: identity), InspectorSection(title: "Progress", rows: progress)]
    }

    // MARK: Backend (nothing selected)

    static func backend(_ state: DashboardState) -> [InspectorSection] {
        var rows: [InspectorRow] = []
        let version = state.capabilities?.version ?? state.health?.version
        if let version { rows.append(InspectorRow("Version", version, monospaced: true)) }
        switch state.capabilities?.mode ?? state.health?.mode ?? .unknown {
        case .demo: rows.append(InspectorRow("Mode", "Demo (simulated)", tone: .warning))
        case .live: rows.append(InspectorRow("Mode", "Live", tone: .success))
        case .unknown: break
        }
        if let capabilities = state.capabilities {
            rows.append(InspectorRow("Host", "\(capabilities.platform) \(capabilities.architecture)"))
            if let node = capabilities.node { rows.append(InspectorRow("Node", node, monospaced: true)) }
            if let root = capabilities.workspaceRoot { rows.append(InspectorRow("Workspace", root, monospaced: true, copyable: true)) }
            if let dir = capabilities.dataDir { rows.append(InspectorRow("Data folder", dir, monospaced: true, copyable: true)) }
            if let auth = capabilities.auth { rows.append(InspectorRow("API token", auth.required ? "Required" : "Not required")) }
        }
        if let capacity = state.capacity {
            rows.append(InspectorRow("Capacity", "\(capacity.load) of \(capacity.maxLoad) units"))
            rows.append(InspectorRow("CPU cores", "\(capacity.cpuCores)"))
            rows.append(InspectorRow("Memory", String(format: "%.1f GB", capacity.memoryGb)))
        }
        if let uptime = state.health?.uptimeSeconds { rows.append(InspectorRow("Uptime", Formatters.duration(TimeInterval(uptime)))) }
        return rows.isEmpty ? [] : [InspectorSection(title: "Backend", rows: rows)]
    }

    // MARK: Tones

    static func tone(_ status: DeviceStatus) -> StatusTone {
        switch status {
        case .ready: return .success
        case .error: return .failure
        case .booting, .shuttingDown, .busy, .created: return .running
        case .stopped, .unknown: return .neutral
        }
    }

    static func tone(_ status: JobStatus) -> StatusTone {
        switch status {
        case .completed: return .success
        case .failed: return .failure
        case .running: return .running
        case .retrying: return .warning
        case .queued, .cancelled, .unknown: return .neutral
        }
    }

    static func tone(_ status: RunStatus) -> StatusTone {
        switch status {
        case .passed: return .success
        case .failed: return .failure
        case .running: return .running
        case .queued, .cancelled, .unknown: return .neutral
        }
    }

    static func statusLabel(_ status: JobStatus) -> String {
        switch status {
        case .completed: return "Passed"
        case .queued: return "Queued"
        case .running: return "Running"
        case .retrying: return "Retrying"
        case .failed: return "Failed"
        case .cancelled: return "Cancelled"
        case .unknown: return "Unknown"
        }
    }

    // MARK: History and Quick Help

    /// The History tab: newest first, time and text.
    public static func history(events: [EngineEvent], formatting: DateFormatting) -> [(id: String, time: String, text: String, isError: Bool)] {
        events.filter { !$0.isOutput }.sorted { $0.timestamp > $1.timestamp || ($0.timestamp == $1.timestamp && $0.id > $1.id) }.map {
            (id: $0.dedupeKey, time: formatting.clock($0.timestamp), text: $0.message, isError: $0.isError)
        }
    }

    /// The Quick Help tab: what the selected status means and what can be done next, in plain words.
    public static func quickHelp(for target: EditorTarget, state: DashboardState) -> String {
        switch target {
        case .welcome:
            if state.devices.isEmpty { return "There are no simulators yet. Create one with New Simulator, or just run a scheme: the backend creates a simulator for it when none fits." }
            return "Pick a scheme and a destination in the toolbar, then press Run. Select a simulator, a report or a run in the navigator to see it here."

        case .device(let id):
            guard let device = state.device(id: id) else { return "This simulator is no longer known to the backend." }
            switch device.status {
            case .ready: return "Booted and idle. Run puts the selected scheme on it; Shut Down stops it and frees its capacity units."
            case .busy: return "A job is running on this simulator. Stop cancels the job; the simulator becomes ready again afterwards."
            case .booting: return "Starting up. It becomes ready in a few seconds, and queued jobs that need it start then."
            case .shuttingDown: return "Shutting down. It can be booted again afterwards."
            case .stopped: return "Not running. Boot it, or run tests on it: the backend boots it when a job needs it."
            case .error: return "The last operation failed: \(device.lastError ?? "no details were reported"). Try Boot again, or Delete it and create another."
            case .created, .unknown: return "The backend has just created this simulator."
            }

        case .job(let id):
            guard let job = state.job(id: id) else { return "This job is no longer known to the backend (it may have been cleaned up)." }
            switch job.status {
            case .queued: return "Waiting for a device: \(job.waitingReason ?? "the scheduler has not started it yet"). Stop cancels it."
            case .running: return "Running. Logs streams the xcodebuild output live; Stop cancels the job."
            case .retrying: return "The last attempt failed and the job will run again (\(job.attemptText()))."
            case .completed: return "All tests passed. Run Again repeats it with the same settings."
            case .failed:
                if job.summary?.buildFailed == true { return "The build failed before any test ran. The Logs tab shows the compiler output; fix it and Run Again." }
                return "At least one test failed. The Tests tab lists them, the Logs tab jumps to the failing line."
            case .cancelled: return "Cancelled before it finished. Run Again starts a fresh job."
            case .unknown: return "The backend reported a status this app does not know."
            }

        case .run(let id):
            guard let run = state.run(id: id) else { return "This run is no longer known to the backend." }
            return "A run is one scheme on several destinations (\(run.jobIds.count) jobs). Select a job to see its report; Stop cancels every unfinished job."
        }
    }
}
