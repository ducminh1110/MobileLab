import Foundation

/// One entry of the backend's event stream (`/events`, `/ws/events`).
public struct EngineEvent: Codable, Equatable, Sendable, Identifiable {
    /// Monotonic per backend process. Restarting the backend starts again at 1.
    public var id: Int
    /// `simctl`, `xcodebuild`, `scheduler`, `orchestrator`, `vm` or `system`.
    public var source: String
    /// `started`, `log`, `finished` or `error`.
    public var type: String
    public var action: String
    public var message: String
    public var timestamp: Date
    public var jobId: String?
    public var deviceId: String?
    public var runId: String?
    public var metadata: [String: JSONValue]?

    public init(
        id: Int, source: String, type: String, action: String, message: String, timestamp: Date,
        jobId: String? = nil, deviceId: String? = nil, runId: String? = nil, metadata: [String: JSONValue]? = nil
    ) {
        self.id = id
        self.source = source
        self.type = type
        self.action = action
        self.message = message
        self.timestamp = timestamp
        self.jobId = jobId
        self.deviceId = deviceId
        self.runId = runId
        self.metadata = metadata
    }

    /// Raw xcodebuild output. These are streamed but the backend does not keep them in its history.
    public var isOutput: Bool { action == "output" && source == "xcodebuild" }
    public var isError: Bool { type == "error" }

    /// Two events are the same event when id and timestamp agree; the id alone repeats after a backend restart.
    public var dedupeKey: String { "\(id)@\(timestamp.timeIntervalSince1970)" }
}

public struct EventsResponse: Codable, Equatable, Sendable {
    public var items: [EngineEvent]
    public var lastId: Int?
}
