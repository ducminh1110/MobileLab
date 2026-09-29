import Foundation

public enum BackendMode: String, Codable, Sendable {
    case live
    case demo
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

/// `GET /health`, the only endpoint that stays open when an API token is set.
public struct Health: Codable, Equatable, Sendable {
    public var status: String
    public var timestamp: Date?
    public var version: String?
    public var mode: BackendMode
    public var uptimeSeconds: Int?

    public init(status: String = "ok", timestamp: Date? = nil, version: String? = nil, mode: BackendMode = .unknown, uptimeSeconds: Int? = nil) {
        self.status = status
        self.timestamp = timestamp
        self.version = version
        self.mode = mode
        self.uptimeSeconds = uptimeSeconds
    }

    enum CodingKeys: String, CodingKey { case status, timestamp, version, mode, uptimeSeconds }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        status = try c.decodeIfPresent(String.self, forKey: .status) ?? "unknown"
        timestamp = try c.decodeIfPresent(Date.self, forKey: .timestamp)
        version = try c.decodeIfPresent(String.self, forKey: .version)
        mode = try c.decodeIfPresent(BackendMode.self, forKey: .mode) ?? .unknown
        uptimeSeconds = try c.decodeIfPresent(Int.self, forKey: .uptimeSeconds)
    }
}

public struct VMCapabilities: Codable, Equatable, Sendable {
    public var enabled: Bool
    public var simulated: Bool
    public var canRunTests: Bool
}

public struct AuthCapabilities: Codable, Equatable, Sendable {
    public var required: Bool
}

/// `GET /capabilities`.
public struct BackendCapabilities: Codable, Equatable, Sendable {
    public var platform: String
    public var architecture: String
    public var kernel: String?
    public var node: String?
    public var arm64Linux: Bool?
    public var kvmDevice: Bool?
    public var supportedTargets: [String]
    public var version: String?
    public var mode: BackendMode
    public var modeReason: String?
    public var vm: VMCapabilities?
    public var auth: AuthCapabilities?
    public var workspaceRoot: String?
    public var dataDir: String?
    public var capacity: CapacitySnapshot?

    enum CodingKeys: String, CodingKey {
        case platform, architecture, kernel, node, arm64Linux, kvmDevice, supportedTargets, version, mode, modeReason
        case vm, auth, workspaceRoot, dataDir, capacity
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        platform = try c.decodeIfPresent(String.self, forKey: .platform) ?? "unknown"
        architecture = try c.decodeIfPresent(String.self, forKey: .architecture) ?? "unknown"
        kernel = try c.decodeIfPresent(String.self, forKey: .kernel)
        node = try c.decodeIfPresent(String.self, forKey: .node)
        arm64Linux = try c.decodeIfPresent(Bool.self, forKey: .arm64Linux)
        kvmDevice = try c.decodeIfPresent(Bool.self, forKey: .kvmDevice)
        supportedTargets = try c.decodeIfPresent([String].self, forKey: .supportedTargets) ?? []
        version = try c.decodeIfPresent(String.self, forKey: .version)
        mode = try c.decodeIfPresent(BackendMode.self, forKey: .mode) ?? .unknown
        modeReason = try c.decodeIfPresent(String.self, forKey: .modeReason)
        vm = try c.decodeIfPresent(VMCapabilities.self, forKey: .vm)
        auth = try c.decodeIfPresent(AuthCapabilities.self, forKey: .auth)
        workspaceRoot = try c.decodeIfPresent(String.self, forKey: .workspaceRoot)
        dataDir = try c.decodeIfPresent(String.self, forKey: .dataDir)
        capacity = try c.decodeIfPresent(CapacitySnapshot.self, forKey: .capacity)
    }
}

// MARK: - Catalog

public struct CatalogRuntime: Codable, Equatable, Sendable, Identifiable {
    public var identifier: String
    public var name: String
    public var version: String
    /// Device type identifiers this runtime can host; nil or empty means the backend did not say.
    public var supportedDeviceTypes: [String]?

    public var id: String { identifier }

    public init(identifier: String, name: String, version: String, supportedDeviceTypes: [String]? = nil) {
        self.identifier = identifier
        self.name = name
        self.version = version
        self.supportedDeviceTypes = supportedDeviceTypes
    }
}

public struct CatalogDeviceType: Codable, Equatable, Sendable, Identifiable {
    public var identifier: String
    public var name: String
    public var family: String

    public var id: String { identifier }

    public init(identifier: String, name: String, family: String) {
        self.identifier = identifier
        self.name = name
        self.family = family
    }
}

/// `GET /catalog`: what this host can actually create.
public struct Catalog: Codable, Equatable, Sendable {
    public var runtimes: [CatalogRuntime]
    public var deviceTypes: [CatalogDeviceType]
    public var source: String?

    public init(runtimes: [CatalogRuntime], deviceTypes: [CatalogDeviceType], source: String? = nil) {
        self.runtimes = runtimes
        self.deviceTypes = deviceTypes
        self.source = source
    }

    /// A device type the runtime cannot host does not exist (a new iPhone on an old iOS).
    public func supports(runtime: CatalogRuntime, deviceType: CatalogDeviceType) -> Bool {
        guard let supported = runtime.supportedDeviceTypes, !supported.isEmpty else { return true }
        return supported.contains(deviceType.identifier)
    }

    public func deviceTypes(for runtime: CatalogRuntime) -> [CatalogDeviceType] {
        deviceTypes.filter { supports(runtime: runtime, deviceType: $0) }
    }

    public func runtime(identifier: String) -> CatalogRuntime? { runtimes.first { $0.identifier == identifier } }
    public func deviceType(identifier: String) -> CatalogDeviceType? { deviceTypes.first { $0.identifier == identifier } }
}

// MARK: - Doctor

public enum CheckStatus: String, Codable, Sendable {
    case ok
    case warn
    case fail
    case skip
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

public struct DoctorCheck: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var name: String
    public var status: CheckStatus
    public var message: String
    /// What to do about it.
    public var remedy: String?
}

public enum DoctorStatus: String, Codable, Sendable {
    case healthy
    case degraded
    case unhealthy
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

/// `GET /doctor`.
public struct DoctorReport: Codable, Equatable, Sendable {
    public var status: DoctorStatus
    public var mode: BackendMode
    public var generatedAt: Date?
    public var checks: [DoctorCheck]
}

// MARK: - Metrics and maintenance

public struct ProcessMetrics: Codable, Equatable, Sendable {
    public var pid: Int
    public var uptimeSeconds: Int
    public var rssBytes: Int
    /// 100 means one full core.
    public var cpuPercent: Double
}

/// `GET /metrics/summary`.
public struct MetricsSummary: Codable, Equatable, Sendable {
    public var devices: Int
    public var jobs: Int
    public var queueDepth: Int
    public var running: Int
    public var jobsByStatus: [String: Int]
    public var capacity: CapacitySnapshot?
    public var hostMemoryFreeGb: Double?
    public var artifactBytes: Int
    public var process: ProcessMetrics?

    enum CodingKeys: String, CodingKey {
        case devices, jobs, queueDepth, running, jobsByStatus, capacity, hostMemoryFreeGb, artifactBytes, process
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        devices = try c.decodeIfPresent(Int.self, forKey: .devices) ?? 0
        jobs = try c.decodeIfPresent(Int.self, forKey: .jobs) ?? 0
        queueDepth = try c.decodeIfPresent(Int.self, forKey: .queueDepth) ?? 0
        running = try c.decodeIfPresent(Int.self, forKey: .running) ?? 0
        jobsByStatus = try c.decodeIfPresent([String: Int].self, forKey: .jobsByStatus) ?? [:]
        capacity = try c.decodeIfPresent(CapacitySnapshot.self, forKey: .capacity)
        hostMemoryFreeGb = try c.decodeIfPresent(Double.self, forKey: .hostMemoryFreeGb)
        artifactBytes = try c.decodeIfPresent(Int.self, forKey: .artifactBytes) ?? 0
        process = try c.decodeIfPresent(ProcessMetrics.self, forKey: .process)
    }
}

/// `POST /maintenance/cleanup`.
public struct CleanupResult: Codable, Equatable, Sendable {
    public var jobsRemoved: Int
    public var artifactsRemoved: Int
    public var bytesFreed: Int
    public var runsRemoved: Int
}

/// `POST /devices/sync`.
public struct SyncResult: Codable, Equatable, Sendable {
    public var removed: Int
    public var updated: Int
}
