import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

// MARK: - Abstraction over the socket

public enum WebSocketMessage: Sendable, Equatable {
    case text(String)
    case binary(Data)
}

public struct WebSocketClosed: Error, Equatable, Sendable, LocalizedError {
    public var code: Int
    public var reason: String?

    public init(code: Int, reason: String? = nil) {
        self.code = code
        self.reason = reason
    }

    public var errorDescription: String? {
        if let reason, !reason.isEmpty { return "Connection closed (\(code)): \(reason)" }
        return "Connection closed (\(code))"
    }
}

/// One WebSocket connection. The real one wraps `URLSessionWebSocketTask`; tests script a fake.
public protocol WebSocketConnection: Sendable {
    /// Returns once the server accepted the connection, or throws when it could not connect.
    func waitUntilOpen() async throws
    /// The next frame; throws when the connection ends (also after `close()`).
    func receive() async throws -> WebSocketMessage
    func close()
}

public protocol WebSocketConnector: Sendable {
    /// Starts connecting. Failures show up in `waitUntilOpen()`.
    func connect(url: URL, headers: [String: String]) -> WebSocketConnection
}

// MARK: - Reconnect policy

/// Exponential backoff between reconnect attempts: 1s, 2s, 4s ... up to `maxDelay`, with a little jitter so
/// several clients do not hammer a restarting backend in step.
public struct ReconnectBackoff: Equatable, Sendable {
    public var initialDelay: TimeInterval
    public var maxDelay: TimeInterval
    public var multiplier: Double
    /// 0.2 means +-20%.
    public var jitter: Double

    public init(initialDelay: TimeInterval = 1, maxDelay: TimeInterval = 30, multiplier: Double = 2, jitter: Double = 0.2) {
        self.initialDelay = initialDelay
        self.maxDelay = maxDelay
        self.multiplier = multiplier
        self.jitter = jitter
    }

    /// `attempt` counts consecutive failures, starting at 0. `random` is in 0..<1; 0.5 adds no jitter.
    public func delay(attempt: Int, random: Double = 0.5) -> TimeInterval {
        let exponent = Double(max(0, min(attempt, 30)))
        let base = min(maxDelay, initialDelay * pow(multiplier, exponent))
        let spread = 1 + jitter * (2 * min(max(random, 0), 1) - 1)
        return max(0, min(maxDelay * (1 + jitter), base * spread))
    }
}

// MARK: - Stream

/// What to subscribe to. Everything is optional: no filter means every event of the backend.
public struct EventStreamOptions: Equatable, Sendable {
    public var jobID: String?
    public var deviceID: String?
    public var runID: String?
    /// Raw xcodebuild output lines (`output=1`). Chatty: only ask for it for the job that is open.
    public var includeOutput: Bool
    /// Ask the server to replay this many recent matching events first.
    public var replay: Int?

    public init(jobID: String? = nil, deviceID: String? = nil, runID: String? = nil, includeOutput: Bool = false, replay: Int? = 100) {
        self.jobID = jobID
        self.deviceID = deviceID
        self.runID = runID
        self.includeOutput = includeOutput
        self.replay = replay
    }

    public var query: [URLQueryItem] {
        var items: [URLQueryItem] = []
        if let jobID { items.append(URLQueryItem(name: "jobId", value: jobID)) }
        if let deviceID { items.append(URLQueryItem(name: "deviceId", value: deviceID)) }
        if let runID { items.append(URLQueryItem(name: "runId", value: runID)) }
        if includeOutput { items.append(URLQueryItem(name: "output", value: "1")) }
        if let replay { items.append(URLQueryItem(name: "replay", value: String(replay))) }
        return items
    }
}

public enum StreamSignal: Equatable, Sendable {
    /// About to connect. `attempt` is 0 for the first try and counts consecutive failures after that.
    case connecting(attempt: Int)
    case connected
    case event(EngineEvent)
    /// A frame that is not an event; ignored by the reducer, kept for diagnostics.
    case malformed(String)
    case disconnected(reason: String, retryIn: TimeInterval)
}

/// Subscribes to `/ws/events` and keeps the subscription alive: on any failure it waits (exponential backoff)
/// and connects again, until the consumer stops iterating or its task is cancelled.
///
/// There is no reference cycle to worry about: the client holds no task and no stream. Each call to `signals`
/// starts a task that owns only value-type copies of what it needs; the stream's termination cancels it, and the
/// cancellation handler closes the socket so a blocked `receive()` returns.
public final class EventStreamClient: Sendable {
    public typealias Sleep = @Sendable (TimeInterval) async throws -> Void

    private let api: APIClient
    private let connector: WebSocketConnector
    private let backoff: ReconnectBackoff
    private let sleep: Sleep
    private let random: @Sendable () -> Double
    private let now: @Sendable () -> Date
    private let stableAfter: TimeInterval

    public init(
        api: APIClient,
        connector: WebSocketConnector,
        backoff: ReconnectBackoff = ReconnectBackoff(),
        stableAfter: TimeInterval = 10,
        sleep: @escaping Sleep = { seconds in try await Task.sleep(nanoseconds: UInt64(max(0, seconds) * 1_000_000_000)) },
        random: @escaping @Sendable () -> Double = { Double.random(in: 0..<1) },
        now: @escaping @Sendable () -> Date = { Date() }
    ) {
        self.api = api
        self.connector = connector
        self.backoff = backoff
        self.stableAfter = stableAfter
        self.sleep = sleep
        self.random = random
        self.now = now
    }

    public func signals(_ options: EventStreamOptions = EventStreamOptions()) -> AsyncStream<StreamSignal> {
        let api = self.api, connector = self.connector, backoff = self.backoff, stableAfter = self.stableAfter
        let sleep = self.sleep, random = self.random, now = self.now
        return AsyncStream(StreamSignal.self, bufferingPolicy: .unbounded) { continuation in
            let task = Task {
                await Self.run(api: api, connector: connector, backoff: backoff, stableAfter: stableAfter, sleep: sleep, random: random, now: now, options: options, continuation: continuation)
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    private static func run(
        api: APIClient, connector: WebSocketConnector, backoff: ReconnectBackoff, stableAfter: TimeInterval,
        sleep: Sleep, random: @Sendable () -> Double, now: @Sendable () -> Date,
        options: EventStreamOptions, continuation: AsyncStream<StreamSignal>.Continuation
    ) async {
        let decoder = BackendJSON.decoder()
        var attempt = 0

        while !Task.isCancelled {
            continuation.yield(.connecting(attempt: attempt))
            guard let url = api.webSocketURL(query: options.query) else {
                continuation.yield(.disconnected(reason: "Not a valid backend address: \(api.baseURL.absoluteString)", retryIn: backoff.maxDelay))
                break
            }

            let connection = connector.connect(url: url, headers: api.authorizationHeaders)
            var openedAt: Date?
            var reason = "The connection ended."
            do {
                try await withTaskCancellationHandler {
                    try await connection.waitUntilOpen()
                    openedAt = now()
                    continuation.yield(.connected)
                    while true {
                        let message = try await connection.receive()
                        let data: Data
                        switch message {
                        case .text(let text): data = Data(text.utf8)
                        case .binary(let bytes): data = bytes
                        }
                        do {
                            continuation.yield(.event(try decoder.decode(EngineEvent.self, from: data)))
                        } catch {
                            continuation.yield(.malformed(String(decoding: data.prefix(200), as: UTF8.self)))
                        }
                    }
                } onCancel: {
                    connection.close()
                }
            } catch {
                reason = describe(error)
            }
            connection.close()
            if Task.isCancelled { break }

            // A connection that held for a while was healthy: the next failure starts the backoff over.
            if let openedAt, now().timeIntervalSince(openedAt) >= stableAfter { attempt = 0 }
            let delay = backoff.delay(attempt: attempt, random: random())
            attempt += 1
            continuation.yield(.disconnected(reason: reason, retryIn: delay))
            do { try await sleep(delay) } catch { break }
        }
        continuation.finish()
    }

    static func describe(_ error: Error) -> String {
        if let closed = error as? WebSocketClosed { return closed.localizedDescription }
        if let urlError = error as? URLError {
            switch urlError.code {
            case .cannotConnectToHost, .networkConnectionLost, .notConnectedToInternet, .cannotFindHost, .dnsLookupFailed:
                return "Cannot reach the backend."
            case .timedOut:
                return "The backend did not answer in time."
            default:
                return urlError.localizedDescription
            }
        }
        return error.localizedDescription
    }
}

// MARK: - URLSession implementation

public final class URLSessionWebSocketConnector: WebSocketConnector, @unchecked Sendable {
    private let session: URLSession

    public init(session: URLSession? = nil) {
        if let session {
            self.session = session
        } else {
            let configuration = URLSessionConfiguration.ephemeral
            configuration.timeoutIntervalForRequest = 15
            self.session = URLSession(configuration: configuration)
        }
    }

    public func connect(url: URL, headers: [String: String]) -> WebSocketConnection {
        var request = URLRequest(url: url)
        for (name, value) in headers { request.setValue(value, forHTTPHeaderField: name) }
        let task = session.webSocketTask(with: request)
        task.resume()
        return URLSessionWebSocketConnection(task: task)
    }
}

final class URLSessionWebSocketConnection: WebSocketConnection, @unchecked Sendable {
    private let task: URLSessionWebSocketTask

    init(task: URLSessionWebSocketTask) { self.task = task }

    /// A pong proves the handshake completed; a failed connection fails the ping.
    func waitUntilOpen() async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            task.sendPing { error in
                if let error { continuation.resume(throwing: error) } else { continuation.resume() }
            }
        }
    }

    func receive() async throws -> WebSocketMessage {
        // The async overload exists on macOS 12+ (the deployment target is 13) and in swift-corelibs-foundation.
        switch try await task.receive() {
        case .string(let text): return .text(text)
        case .data(let data): return .binary(data)
        @unknown default: return .binary(Data())
        }
    }

    func close() {
        task.cancel(with: .goingAway, reason: nil)
    }
}
