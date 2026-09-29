import Foundation

/// Reads a string-backed enum without failing on values a newer backend may add.
func decodeTolerant<E: RawRepresentable>(_ decoder: Decoder, fallback: E) throws -> E where E.RawValue == String {
    let raw = try decoder.singleValueContainer().decode(String.self)
    return E(rawValue: raw) ?? fallback
}

public enum DeviceStatus: String, Codable, Sendable, CaseIterable {
    case created
    case booting
    case ready
    case busy
    case shuttingDown = "shutting_down"
    case stopped
    case error
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }

    /// Booted devices occupy memory and can take a job.
    public var isBooted: Bool { self == .ready || self == .busy }
    public var isTransitioning: Bool { self == .booting || self == .shuttingDown || self == .created }

    public var label: String {
        switch self {
        case .created: return "Created"
        case .booting: return "Booting"
        case .ready: return "Ready"
        case .busy: return "Busy"
        case .shuttingDown: return "Shutting Down"
        case .stopped: return "Stopped"
        case .error: return "Error"
        case .unknown: return "Unknown"
        }
    }
}

public enum DeviceKind: String, Codable, Sendable {
    case simulator
    case vm
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

/// `simctl` drives real Xcode simulators; `simulated` never touches a real device.
public enum DeviceBackend: String, Codable, Sendable {
    case simctl
    case simulated
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

public struct Device: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var name: String
    /// Runtime identifier, e.g. `com.apple.CoreSimulator.SimRuntime.iOS-18-0`.
    public var runtime: String
    /// Human readable runtime, e.g. `iOS 18.0`.
    public var runtimeName: String?
    public var modelId: String?
    public var modelName: String?
    public var status: DeviceStatus
    public var type: DeviceKind
    public var backend: DeviceBackend
    public var canRunTests: Bool
    /// Created automatically for a run; removed again when no queued job needs it.
    public var ephemeral: Bool
    public var simulatorUdid: String?
    public var currentJobId: String?
    public var lastError: String?

    // Experimental (simulated) VM attributes.
    public var variant: String?
    public var currentPatchTier: String?
    public var backupList: [String]?
    public var cpu: Int?
    public var memory: Int?
    public var disk: Int?
    public var screen: String?

    public var createdAt: Date?
    public var updatedAt: Date?

    public init(
        id: String, name: String, runtime: String, runtimeName: String? = nil, modelId: String? = nil, modelName: String? = nil,
        status: DeviceStatus, type: DeviceKind = .simulator, backend: DeviceBackend = .simctl, canRunTests: Bool = true,
        ephemeral: Bool = false, simulatorUdid: String? = nil, currentJobId: String? = nil, lastError: String? = nil,
        createdAt: Date? = nil, updatedAt: Date? = nil
    ) {
        self.id = id
        self.name = name
        self.runtime = runtime
        self.runtimeName = runtimeName
        self.modelId = modelId
        self.modelName = modelName
        self.status = status
        self.type = type
        self.backend = backend
        self.canRunTests = canRunTests
        self.ephemeral = ephemeral
        self.simulatorUdid = simulatorUdid
        self.currentJobId = currentJobId
        self.lastError = lastError
        self.createdAt = createdAt
        self.updatedAt = updatedAt
    }

    enum CodingKeys: String, CodingKey {
        case id, name, runtime, runtimeName, modelId, modelName, status, type, backend, canRunTests, ephemeral
        case simulatorUdid, currentJobId, lastError, variant, currentPatchTier, backupList, cpu, memory, disk, screen
        case createdAt, updatedAt
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        runtime = try c.decodeIfPresent(String.self, forKey: .runtime) ?? ""
        runtimeName = try c.decodeIfPresent(String.self, forKey: .runtimeName)
        modelId = try c.decodeIfPresent(String.self, forKey: .modelId)
        modelName = try c.decodeIfPresent(String.self, forKey: .modelName)
        status = try c.decodeIfPresent(DeviceStatus.self, forKey: .status) ?? .unknown
        type = try c.decodeIfPresent(DeviceKind.self, forKey: .type) ?? .simulator
        backend = try c.decodeIfPresent(DeviceBackend.self, forKey: .backend) ?? .unknown
        canRunTests = try c.decodeIfPresent(Bool.self, forKey: .canRunTests) ?? false
        ephemeral = try c.decodeIfPresent(Bool.self, forKey: .ephemeral) ?? false
        simulatorUdid = try c.decodeIfPresent(String.self, forKey: .simulatorUdid)
        currentJobId = try c.decodeIfPresent(String.self, forKey: .currentJobId)
        lastError = try c.decodeIfPresent(String.self, forKey: .lastError)
        variant = try c.decodeIfPresent(String.self, forKey: .variant)
        currentPatchTier = try c.decodeIfPresent(String.self, forKey: .currentPatchTier)
        backupList = try c.decodeIfPresent([String].self, forKey: .backupList)
        cpu = try c.decodeIfPresent(Int.self, forKey: .cpu)
        memory = try c.decodeIfPresent(Int.self, forKey: .memory)
        disk = try c.decodeIfPresent(Int.self, forKey: .disk)
        screen = try c.decodeIfPresent(String.self, forKey: .screen)
        createdAt = try c.decodeIfPresent(Date.self, forKey: .createdAt)
        updatedAt = try c.decodeIfPresent(Date.self, forKey: .updatedAt)
    }

    /// `iOS 18.0`, or the last component of the identifier when the backend sent no name.
    public var runtimeDisplayName: String {
        if let runtimeName, !runtimeName.isEmpty { return runtimeName }
        return RuntimeName.display(fromIdentifier: runtime)
    }

    public var isVM: Bool { type == .vm }

    /// Families are told apart by name because the API only reports the device type identifier.
    public var isTablet: Bool {
        let lowered = (modelName ?? modelId ?? name).lowercased()
        return lowered.contains("ipad")
    }
}

public enum RuntimeName {
    /// `com.apple.CoreSimulator.SimRuntime.iOS-18-0` becomes `iOS 18.0`; anything else is returned as is.
    public static func display(fromIdentifier identifier: String) -> String {
        let marker = "SimRuntime."
        guard let range = identifier.range(of: marker) else { return identifier }
        let tail = identifier[range.upperBound...]
        let parts = tail.split(separator: "-", omittingEmptySubsequences: true).map(String.init)
        guard let platform = parts.first, parts.count >= 2 else { return String(tail) }
        return platform + " " + parts.dropFirst().joined(separator: ".")
    }

    /// Numeric sort key for `iOS 18.0`, `iOS 17.5` and identifiers: `[18, 0]`.
    public static func versionComponents(_ name: String) -> [Int] {
        let display = name.contains("SimRuntime.") ? Self.display(fromIdentifier: name) : name
        let numbers = display.split(whereSeparator: { !$0.isNumber }).compactMap { Int($0) }
        return numbers
    }
}

public struct CapacitySnapshot: Codable, Equatable, Sendable {
    public var maxLoad: Int
    public var load: Int
    public var cpuCores: Int
    public var memoryGb: Double

    public init(maxLoad: Int, load: Int, cpuCores: Int, memoryGb: Double) {
        self.maxLoad = maxLoad
        self.load = load
        self.cpuCores = cpuCores
        self.memoryGb = memoryGb
    }

    /// 0...1, or nil when the backend reports no limit.
    public var fraction: Double? {
        maxLoad > 0 ? min(1, max(0, Double(load) / Double(maxLoad))) : nil
    }
}

public struct DevicesResponse: Codable, Equatable, Sendable {
    public var items: [Device]
    public var capacity: CapacitySnapshot?

    public init(items: [Device], capacity: CapacitySnapshot? = nil) {
        self.items = items
        self.capacity = capacity
    }
}
