import XCTest
@testable import IOSLabDashboardCore

/// A scripted WebSocket. `hang` blocks in `receive()` until `close()` is called, like a quiet real connection.
final class FakeSocket: WebSocketConnection, @unchecked Sendable {
    enum Step {
        case message(String)
        case fail(Error)
        case hang
    }

    private let lock = NSLock()
    private var steps: [Step]
    private let openError: Error?
    private var waiting: CheckedContinuation<WebSocketMessage, Error>?
    private var closed = false

    init(openError: Error? = nil, steps: [Step] = [.hang]) {
        self.openError = openError
        self.steps = steps
    }

    var isClosed: Bool { lock.withLock { closed } }

    func waitUntilOpen() async throws {
        if let openError { throw openError }
    }

    func receive() async throws -> WebSocketMessage {
        let step: Step? = lock.withLock {
            if closed { return .fail(URLError(.cancelled)) }
            return steps.isEmpty ? Step.hang : steps.removeFirst()
        }
        switch step {
        case .message(let text)?: return .text(text)
        case .fail(let error)?: throw error
        default:
            return try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<WebSocketMessage, Error>) in
                let alreadyClosed: Bool = lock.withLock {
                    if closed { return true }
                    waiting = continuation
                    return false
                }
                if alreadyClosed { continuation.resume(throwing: URLError(.cancelled)) }
            }
        }
    }

    func close() {
        let continuation: CheckedContinuation<WebSocketMessage, Error>? = lock.withLock {
            closed = true
            let pending = waiting
            waiting = nil
            return pending
        }
        continuation?.resume(throwing: URLError(.cancelled))
    }
}

final class FakeConnector: WebSocketConnector, @unchecked Sendable {
    private let lock = NSLock()
    private var sockets: [FakeSocket]
    private(set) var urls: [URL] = []
    private(set) var headers: [[String: String]] = []
    private(set) var created: [FakeSocket] = []

    init(_ sockets: [FakeSocket]) { self.sockets = sockets }

    func connect(url: URL, headers: [String: String]) -> WebSocketConnection {
        lock.withLock {
            urls.append(url)
            self.headers.append(headers)
            let socket = sockets.isEmpty ? FakeSocket() : sockets.removeFirst()
            created.append(socket)
            return socket
        }
    }

    var connectCount: Int { lock.withLock { urls.count } }
}

/// Collects delays without waiting.
final class SleepRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var values: [TimeInterval] = []
    var delays: [TimeInterval] { lock.withLock { values } }

    var sleep: EventStreamClient.Sleep {
        { [self] seconds in
            lock.withLock { values.append(seconds) }
            await Task.yield()
        }
    }
}

func withTimeout<T: Sendable>(_ seconds: Double = 5, _ operation: @escaping @Sendable () async throws -> T) async throws -> T {
    try await withThrowingTaskGroup(of: T.self) { group in
        group.addTask { try await operation() }
        group.addTask {
            try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
            throw TimeoutError()
        }
        let first = try await group.next()!
        group.cancelAll()
        return first
    }
}

struct TimeoutError: Error {}

func collect(_ stream: AsyncStream<StreamSignal>, until stop: (StreamSignal) -> Bool) async -> [StreamSignal] {
    var seen: [StreamSignal] = []
    for await signal in stream {
        seen.append(signal)
        if stop(signal) { break }
    }
    return seen
}

func eventJSON(id: Int, action: String = "job_finished", message: String = "done", jobId: String? = "j1") -> String {
    let job = jobId.map { #","jobId":"\#($0)""# } ?? ""
    return #"{"source":"scheduler","type":"log","action":"\#(action)","message":"\#(message)","id":\#(id),"timestamp":"2026-09-29T16:10:0\#(id % 10).000Z"\#(job)}"#
}

final class EventStreamTests: XCTestCase {
    private func api(token: String? = nil) -> APIClient {
        APIClient(baseURL: URL(string: "http://127.0.0.1:4000")!, token: token, transport: FakeTransport())
    }

    func testBackoffGrowsAndIsCapped() {
        let backoff = ReconnectBackoff(initialDelay: 1, maxDelay: 30, multiplier: 2, jitter: 0)
        XCTAssertEqual((0..<8).map { backoff.delay(attempt: $0) }, [1, 2, 4, 8, 16, 30, 30, 30])
        XCTAssertEqual(backoff.delay(attempt: 500), 30, "a huge attempt count must not overflow")
        XCTAssertEqual(backoff.delay(attempt: -3), 1)
    }

    func testJitterStaysWithinBounds() {
        let backoff = ReconnectBackoff(initialDelay: 2, maxDelay: 30, multiplier: 2, jitter: 0.25)
        XCTAssertEqual(backoff.delay(attempt: 1, random: 0), 3, accuracy: 0.0001)
        XCTAssertEqual(backoff.delay(attempt: 1, random: 0.5), 4, accuracy: 0.0001)
        XCTAssertEqual(backoff.delay(attempt: 1, random: 1), 5, accuracy: 0.0001)
        for step in 0...20 {
            let value = backoff.delay(attempt: 9, random: Double(step) / 20)
            XCTAssertLessThanOrEqual(value, 30 * 1.25)
            XCTAssertGreaterThanOrEqual(value, 0)
        }
    }

    func testOptionsBuildTheQuery() {
        XCTAssertEqual(EventStreamOptions().query.map { "\($0.name)=\($0.value ?? "")" }, ["replay=100"])
        let options = EventStreamOptions(jobID: "j1", deviceID: "d1", runID: "r1", includeOutput: true, replay: nil)
        XCTAssertEqual(options.query.map { "\($0.name)=\($0.value ?? "")" }, ["jobId=j1", "deviceId=d1", "runId=r1", "output=1"])
    }

    func testDeliversEventsInOrderAfterConnecting() async throws {
        let socket = FakeSocket(steps: [.message(eventJSON(id: 1)), .message(eventJSON(id: 2)), .message(eventJSON(id: 3)), .hang])
        let connector = FakeConnector([socket])
        let client = EventStreamClient(api: api(token: "tok"), connector: connector, sleep: SleepRecorder().sleep)

        let signals = try await withTimeout {
            await collect(client.signals(EventStreamOptions(jobID: "j1", includeOutput: true))) { if case .event(let e) = $0 { return e.id == 3 } else { return false } }
        }
        XCTAssertEqual(signals.prefix(2), [.connecting(attempt: 0), .connected])
        XCTAssertEqual(signals.compactMap { signal -> Int? in if case .event(let e) = signal { return e.id } else { return nil } }, [1, 2, 3])

        XCTAssertEqual(connector.urls.first?.absoluteString, "ws://127.0.0.1:4000/ws/events?jobId=j1&output=1&replay=100")
        XCTAssertEqual(connector.headers.first?["Authorization"], "Bearer tok", "the token goes in a header, not in the URL")
        XCTAssertFalse(connector.urls.first?.absoluteString.contains("tok") ?? true)
    }

    func testStopsIteratingClosesTheSocket() async throws {
        let socket = FakeSocket(steps: [.message(eventJSON(id: 1)), .hang])
        let client = EventStreamClient(api: api(), connector: FakeConnector([socket]), sleep: SleepRecorder().sleep)
        _ = try await withTimeout {
            await collect(client.signals()) { if case .event = $0 { return true } else { return false } }
        }
        // Leaving the loop terminates the stream, which cancels the task, whose handler closes the socket.
        for _ in 0..<200 where !socket.isClosed { try await Task.sleep(nanoseconds: 10_000_000) }
        XCTAssertTrue(socket.isClosed)
    }

    func testCancellingTheConsumerClosesTheSocketAndEndsTheStream() async throws {
        let socket = FakeSocket(steps: [.hang])
        let client = EventStreamClient(api: api(), connector: FakeConnector([socket]), sleep: SleepRecorder().sleep)
        let received = Counter()
        let consumer = Task { () -> Int in
            var count = 0
            for await _ in client.signals() {
                count += 1
                _ = received.next()
            }
            return count
        }
        for _ in 0..<300 where received.current < 2 { try await Task.sleep(nanoseconds: 10_000_000) }
        XCTAssertGreaterThanOrEqual(received.current, 2, "connecting and connected were delivered")
        consumer.cancel()
        let count = try await withTimeout { await consumer.value }
        XCTAssertGreaterThanOrEqual(count, 2)
        XCTAssertTrue(socket.isClosed, "cancelling must close the socket, otherwise receive() would block forever")
    }

    func testReconnectsWithGrowingDelays() async throws {
        let sockets = [
            FakeSocket(openError: URLError(.cannotConnectToHost)),
            FakeSocket(steps: [.message(eventJSON(id: 1)), .fail(WebSocketClosed(code: 1006, reason: "abnormal"))]),
            FakeSocket(steps: [.message(eventJSON(id: 2)), .hang])
        ]
        let recorder = SleepRecorder()
        let connector = FakeConnector(sockets)
        let client = EventStreamClient(api: api(), connector: connector, backoff: ReconnectBackoff(initialDelay: 1, maxDelay: 30, multiplier: 2, jitter: 0), sleep: recorder.sleep)

        let signals = try await withTimeout {
            await collect(client.signals()) { if case .event(let e) = $0 { return e.id == 2 } else { return false } }
        }

        XCTAssertEqual(signals, [
            .connecting(attempt: 0),
            .disconnected(reason: "Cannot reach the backend.", retryIn: 1),
            .connecting(attempt: 1),
            .connected,
            .event(try BackendJSON.decoder().decode(EngineEvent.self, from: Data(eventJSON(id: 1).utf8))),
            .disconnected(reason: "Connection closed (1006): abnormal", retryIn: 2),
            .connecting(attempt: 2),
            .connected,
            .event(try BackendJSON.decoder().decode(EngineEvent.self, from: Data(eventJSON(id: 2).utf8)))
        ])
        XCTAssertEqual(recorder.delays, [1, 2])
        XCTAssertEqual(connector.connectCount, 3)
    }

    func testABackendThatStaysUpResetsTheBackoff() async throws {
        let sockets = [
            FakeSocket(openError: URLError(.cannotConnectToHost)),
            FakeSocket(steps: [.fail(WebSocketClosed(code: 1001))]),
            FakeSocket(steps: [.fail(WebSocketClosed(code: 1001))]),
            FakeSocket()
        ]
        // The clock jumps a minute every time it is read, so any open connection counts as having been stable.
        let ticks = Counter()
        let recorder = SleepRecorder()
        let client = EventStreamClient(
            api: api(), connector: FakeConnector(sockets), backoff: ReconnectBackoff(initialDelay: 1, maxDelay: 30, multiplier: 2, jitter: 0),
            stableAfter: 10, sleep: recorder.sleep, now: { Date(timeIntervalSince1970: Double(ticks.next()) * 60) }
        )
        _ = try await withTimeout {
            await collect(client.signals()) { if case .connecting = $0 { return recorder.delays.count == 3 } else { return false } }
        }
        XCTAssertEqual(recorder.delays, [1, 1, 1], "after a stable connection the delay starts over at 1s")
    }

    func testAFlappingBackendKeepsBackingOff() async throws {
        let sockets = (0..<5).map { _ in FakeSocket(steps: [.fail(WebSocketClosed(code: 1006))]) } + [FakeSocket()]
        let recorder = SleepRecorder()
        let client = EventStreamClient(
            api: api(), connector: FakeConnector(sockets), backoff: ReconnectBackoff(initialDelay: 1, maxDelay: 8, multiplier: 2, jitter: 0),
            stableAfter: 10, sleep: recorder.sleep, now: { Date(timeIntervalSince1970: 0) }
        )
        _ = try await withTimeout {
            await collect(client.signals()) { if case .connecting(let attempt) = $0 { return attempt >= 5 } else { return false } }
        }
        XCTAssertEqual(recorder.delays, [1, 2, 4, 8, 8], "connections that drop at once must not reset the backoff")
    }

    func testMalformedFramesAreReportedAndSkipped() async throws {
        let socket = FakeSocket(steps: [.message("not json"), .message(#"{"hello":"world"}"#), .message(eventJSON(id: 4)), .hang])
        let client = EventStreamClient(api: api(), connector: FakeConnector([socket]), sleep: SleepRecorder().sleep)
        let signals = try await withTimeout {
            await collect(client.signals()) { if case .event = $0 { return true } else { return false } }
        }
        XCTAssertEqual(signals.filter { if case .malformed = $0 { return true } else { return false } }.count, 2)
        XCTAssertEqual(signals.last, .event(try BackendJSON.decoder().decode(EngineEvent.self, from: Data(eventJSON(id: 4).utf8))))
    }

    func testBinaryFramesAreDecodedToo() async throws {
        struct BinarySocket: WebSocketConnection {
            func waitUntilOpen() async throws {}
            func receive() async throws -> WebSocketMessage { .binary(Data(eventJSON(id: 9).utf8)) }
            func close() {}
        }
        struct Connector: WebSocketConnector { func connect(url: URL, headers: [String: String]) -> WebSocketConnection { BinarySocket() } }
        let client = EventStreamClient(api: api(), connector: Connector(), sleep: SleepRecorder().sleep)
        let signals = try await withTimeout { await collect(client.signals()) { if case .event = $0 { return true } else { return false } } }
        guard case .event(let event) = signals.last else { return XCTFail("no event") }
        XCTAssertEqual(event.id, 9)
    }

    func testReplayedRealCaptureFlowsThroughTheStream() async throws {
        let lines = try Fixture.text("events-ws.ndjson").split(separator: "\n").map(String.init)
        let socket = FakeSocket(steps: lines.map { .message($0) } + [.hang])
        let client = EventStreamClient(api: api(), connector: FakeConnector([socket]), sleep: SleepRecorder().sleep)
        let total = lines.count
        let signals = try await withTimeout {
            var events = 0
            return await collect(client.signals()) { signal in
                if case .event = signal { events += 1 }
                return events == total
            }
        }
        XCTAssertEqual(signals.filter { if case .event = $0 { return true } else { return false } }.count, lines.count)
        XCTAssertFalse(signals.contains { if case .malformed = $0 { return true } else { return false } }, "every line the real backend sent decodes")
    }
}

final class Counter: @unchecked Sendable {
    private let lock = NSLock()
    private var value = 0
    func next() -> Int { lock.withLock { value += 1; return value } }
    var current: Int { lock.withLock { value } }
}
