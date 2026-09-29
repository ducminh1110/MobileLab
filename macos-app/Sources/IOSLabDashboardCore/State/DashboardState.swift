import Foundation

public enum ConnectionState: Equatable, Sendable {
    /// Nothing has been heard from the backend yet.
    case connecting
    case connected
    /// The socket dropped or the backend cannot be reached; a reconnect is scheduled.
    case disconnected(reason: String?)

    public var isConnected: Bool { self == .connected }
}

/// What has to be fetched again. The reducer asks; the model (which owns the network) answers.
public struct RefreshRequest: OptionSet, Sendable, Equatable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    public static let devices = RefreshRequest(rawValue: 1 << 0)
    public static let jobs = RefreshRequest(rawValue: 1 << 1)
    public static let runs = RefreshRequest(rawValue: 1 << 2)
    public static let metrics = RefreshRequest(rawValue: 1 << 3)
    public static let all: RefreshRequest = [.devices, .jobs, .runs]
}

/// Everything the window shows that comes from the backend. A plain value: the model owns one, the reducer
/// mutates it, and tests compare it.
public struct DashboardState: Equatable, Sendable {
    public var connection: ConnectionState = .connecting
    public var health: Health?
    public var capabilities: BackendCapabilities?
    public var catalog: Catalog?

    public var devices: [Device] = []
    public var capacity: CapacitySnapshot?
    public var jobs: [TestJob] = []
    public var runs: [TestRunView] = []
    public var metrics: MetricsSummary?
    /// Parsed results of jobs that were opened or expanded, by job id.
    public var results: [String: JobResults] = [:]

    /// Non-output events, oldest first, bounded.
    public var activity: [EngineEvent] = []
    public var lastEventID: Int = 0

    /// True once each list has been fetched at least once: until then the navigator shows skeleton rows.
    public var devicesLoaded = false
    public var jobsLoaded = false
    public var runsLoaded = false

    private var recentKeys: [String] = []
    private var recentKeySet: Set<String> = []

    public static let activityLimit = 500
    static let recentKeyLimit = 800

    public init() {}

    public var isDemo: Bool { (capabilities?.mode ?? health?.mode) == .demo }
    public var hasLoaded: Bool { devicesLoaded && jobsLoaded && runsLoaded }

    public func device(id: String) -> Device? { devices.first { $0.id == id } }
    public func job(id: String) -> TestJob? { jobs.first { $0.id == id } }
    public func run(id: String) -> TestRunView? { runs.first { $0.id == id } }

    public var bootedSimulatorCount: Int { devices.filter { $0.type == .simulator && $0.status.isBooted }.count }
    public var activeJobs: [TestJob] { jobs.filter { $0.status.isActive } }

    /// Whether anything is queued or running, i.e. whether Stop has something to stop.
    public var hasActiveWork: Bool { jobs.contains { $0.status.isActive } }

    // MARK: Reducer

    /// Applies one change and says what has to be refetched as a consequence.
    @discardableResult
    public mutating func reduce(_ action: DashboardAction) -> RefreshRequest {
        switch action {
        case .health(let value):
            health = value
            return []

        case .capabilities(let value):
            capabilities = value
            if let capacity = value.capacity { self.capacity = capacity }
            return []

        case .catalog(let value):
            catalog = value
            return []

        case .devices(let response):
            devices = response.items
            if let capacity = response.capacity { self.capacity = capacity }
            devicesLoaded = true
            return []

        case .jobs(let items):
            // Newest first, as the backend sorts them; sort again so a merged list is stable too.
            jobs = items.sorted { $0.createdAt > $1.createdAt }
            jobsLoaded = true
            let live = Set(jobs.map(\.id))
            results = results.filter { live.contains($0.key) }
            return []

        case .runs(let items):
            runs = items.sorted { $0.createdAt > $1.createdAt }
            runsLoaded = true
            return []

        case .metrics(let value):
            metrics = value
            if let capacity = value.capacity { self.capacity = capacity }
            return []

        case .results(let jobID, let value):
            if jobs.contains(where: { $0.id == jobID }) { results[jobID] = value }
            return []

        case .upsertJob(let job):
            if let index = jobs.firstIndex(where: { $0.id == job.id }) {
                jobs[index] = job
            } else {
                jobs.insert(job, at: 0)
                jobs.sort { $0.createdAt > $1.createdAt }
            }
            return [.runs]

        case .upsertDevice(let device):
            if let index = devices.firstIndex(where: { $0.id == device.id }) {
                devices[index] = device
            } else {
                devices.append(device)
            }
            return []

        case .removeDevice(let id):
            devices.removeAll { $0.id == id }
            return []

        case .connection(let value):
            connection = value
            // The moment the socket is back, whatever happened in the gap is fetched again.
            if value == .connected { return [.all, .metrics] }
            return []

        case .event(let event):
            return apply(event)
        }
    }

    private mutating func apply(_ event: EngineEvent) -> RefreshRequest {
        // Raw build output is for the open job's log, not for the activity list or for refetching.
        if event.isOutput { return [] }

        // A replay after a reconnect repeats events we already have.
        if recentKeySet.contains(event.dedupeKey) { return [] }
        recentKeySet.insert(event.dedupeKey)
        recentKeys.append(event.dedupeKey)
        if recentKeys.count > Self.recentKeyLimit {
            let overflow = recentKeys.count - Self.recentKeyLimit
            for key in recentKeys.prefix(overflow) { recentKeySet.remove(key) }
            recentKeys.removeFirst(overflow)
        }

        activity.append(event)
        if activity.count > Self.activityLimit { activity.removeFirst(activity.count - Self.activityLimit) }
        lastEventID = event.id

        var request: RefreshRequest = .all
        if event.jobId != nil || event.action == "job_finished" { request.insert(.metrics) }
        return request
    }
}

public enum DashboardAction: Equatable, Sendable {
    case health(Health)
    case capabilities(BackendCapabilities)
    case catalog(Catalog)
    case devices(DevicesResponse)
    case jobs([TestJob])
    case runs([TestRunView])
    case metrics(MetricsSummary)
    case results(jobID: String, JobResults)
    /// A job the backend just returned from an action (run, cancel, rerun): shown before the next refetch.
    case upsertJob(TestJob)
    case upsertDevice(Device)
    case removeDevice(String)
    case connection(ConnectionState)
    case event(EngineEvent)
}

/// How often to poll, given what is on screen. Never faster than once a second.
public enum PollingPolicy {
    public static let minimumInterval: TimeInterval = 1

    /// While the socket is down the lists are polled every 5 s; while it is up only a slow safety refresh runs.
    public static func listInterval(connection: ConnectionState) -> TimeInterval {
        switch connection {
        case .connected: return 30
        case .connecting, .disconnected: return 5
        }
    }

    /// The Debug navigator polls the metrics every 2 s while it is visible, otherwise not at all.
    public static func metricsInterval(debugNavigatorVisible: Bool) -> TimeInterval? {
        debugNavigatorVisible ? 2 : nil
    }
}
