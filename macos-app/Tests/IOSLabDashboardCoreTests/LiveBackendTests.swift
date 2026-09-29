import XCTest
@testable import IOSLabDashboardCore

/// End to end against a running backend, with the real URLSession transport and the real WebSocket connector.
/// Skipped unless a backend URL is given:
///
///     IOSLAB_TEST_BACKEND_URL=http://127.0.0.1:4370 IOSLAB_TEST_TOKEN_BACKEND_URL=http://127.0.0.1:4371 IOSLAB_TEST_TOKEN=secret swift test
///
/// Start the backends with `IOSLAB_SIMULATOR_MOCK=true` (demo mode) so results are deterministic; the second one with
/// `IOSLAB_API_TOKEN` set. `Tests/.../Fixtures/capture.sh` shows how.
final class LiveBackendTests: XCTestCase {
    private func backendURL() throws -> URL {
        guard let text = ProcessInfo.processInfo.environment["IOSLAB_TEST_BACKEND_URL"], let url = URL(string: text) else {
            throw XCTSkip("set IOSLAB_TEST_BACKEND_URL to run the live backend tests")
        }
        return url
    }

    private func waitForJob(_ client: APIClient, _ id: String, until done: (TestJob) -> Bool, timeout: TimeInterval = 30) async throws -> TestJob {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            let job = try await client.job(id: id)
            if done(job) { return job }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        throw TimeoutError()
    }

    func testRealTransportRoundTrip() async throws {
        let client = APIClient(baseURL: try backendURL())

        let health = try await client.health()
        XCTAssertEqual(health.status, "ok")
        XCTAssertEqual(health.mode, .demo)

        let capabilities = try await client.capabilities()
        XCTAssertEqual(capabilities.mode, .demo)
        let catalog = try await client.catalog()
        XCTAssertFalse(catalog.runtimes.isEmpty)
        let doctor = try await client.doctor()
        XCTAssertFalse(doctor.checks.isEmpty)
        let metrics = try await client.metrics()
        XCTAssertNotNil(metrics.process)

        // A missing job is the server's message, not a transport error.
        do {
            _ = try await client.job(id: "does-not-exist")
            XCTFail("expected a 404")
        } catch let error as APIError {
            XCTAssertEqual(error.statusCode, 404)
            XCTAssertEqual(error.message, "Job not found: does-not-exist")
        }
    }

    func testRunAFailingSchemeAndReadEverythingBack() async throws {
        let client = APIClient(baseURL: try backendURL())
        let job = try await client.runTest(RunTestRequest(testTarget: "LiveFailTests"))
        let done = try await waitForJob(client, job.id, until: { $0.status.isTerminal })
        XCTAssertEqual(done.status, .failed)
        XCTAssertEqual(done.summary?.failed, 2)

        let results = try await client.results(jobID: job.id)
        XCTAssertEqual(results.cases.count, 8)
        XCTAssertEqual(results.cases.filter { $0.status == .failed }.count, 2)

        let output = try await client.output(jobID: job.id)
        let document = LogDocument(text: output.text)
        XCTAssertEqual(document.lines.filter { $0.info.gutter == .failed }.count, 2)
        XCTAssertEqual(document.lines.filter { $0.info.kind == .failure }.count, 2)

        let artifacts = try await client.artifacts(jobID: job.id)
        XCTAssertTrue(artifacts.contains { $0.type == .log })
        let junit = try await client.junit(jobID: job.id)
        XCTAssertTrue(junit.contains("<failure"))

        let rerun = try await client.rerun(jobID: job.id)
        XCTAssertNotEqual(rerun.id, job.id)
        _ = try await waitForJob(client, rerun.id, until: { $0.status.isTerminal })

        // The status text the toolbar would show for what really happened.
        var state = DashboardState()
        state.reduce(.jobs(try await client.jobs()))
        state.reduce(.devices(try await client.devices()))
        state.reduce(.runs(try await client.runs()))
        state.reduce(.connection(.connected))
        let status = CapsuleStatus.make(scheme: "LiveFailTests", state: state, now: Date(), formatting: DateFormatting())
        XCTAssertEqual(status.text, "Tests Failed | 2 of 8 tests failed")
    }

    func testScreenshotOfARunningSimulator() async throws {
        let client = APIClient(baseURL: try backendURL())
        let device = try await client.spawnDevice(SpawnDeviceRequest(name: "Live Screenshot"))
        defer { Task { try? await client.deleteDevice(id: device.id) } }
        XCTAssertEqual(device.status, .ready)
        let png = try await client.screenshot(deviceID: device.id)
        XCTAssertEqual(Array(png.prefix(4)), [0x89, 0x50, 0x4E, 0x47])
    }

    func testMatrixRunWithSkippedCombination() async throws {
        let client = APIClient(baseURL: try backendURL())
        let response = try await client.createRun(CreateRunRequest(testTarget: "LiveMatrix", runtimes: ["17.5"], models: ["iPhone 15", "iPhone 16 Pro"]))
        XCTAssertEqual(response.skipped.count, 1)
        XCTAssertEqual(response.jobs.count, 1)
        let deadline = Date().addingTimeInterval(30)
        var run = response.run
        while Date() < deadline, !run.status.isFinished {
            try await Task.sleep(nanoseconds: 100_000_000)
            run = try await client.run(id: response.run.id).run
        }
        XCTAssertEqual(run.status, .passed)
    }

    func testWebSocketStreamDeliversLiveEventsAndOutput() async throws {
        let base = try backendURL()
        let client = APIClient(baseURL: base)
        let stream = EventStreamClient(api: client, connector: URLSessionWebSocketConnector())

        // Subscribe first, then start a job, and read until it finishes.
        let collected = Box()
        let reader = Task {
            for await signal in stream.signals(EventStreamOptions(includeOutput: true, replay: 0)) {
                collected.add(signal)
                if case .event(let event) = signal, event.action == "job_finished", collected.sawOutput { break }
            }
        }
        for _ in 0..<100 where !collected.connected { try await Task.sleep(nanoseconds: 50_000_000) }
        XCTAssertTrue(collected.connected, "the WebSocket handshake completed (waitUntilOpen returned)")

        let job = try await client.runTest(RunTestRequest(testTarget: "LiveStreamTests"))
        let finished = try await withTimeout(30) { await reader.value; return true }
        XCTAssertTrue(finished)

        let signals = collected.signals
        XCTAssertEqual(signals.first, .connecting(attempt: 0))
        XCTAssertTrue(signals.contains(.connected))
        let events = signals.compactMap { signal -> EngineEvent? in if case .event(let e) = signal { return e } else { return nil } }
        XCTAssertTrue(events.contains { $0.action == "enqueue_job" && $0.jobId == job.id })
        XCTAssertTrue(events.contains { $0.action == "run_job" && $0.jobId == job.id })
        let output = events.filter { $0.isOutput && $0.jobId == job.id }
        XCTAssertGreaterThan(output.count, 10)
        XCTAssertFalse(signals.contains { if case .malformed = $0 { return true } else { return false } })

        // Feed the real stream through the reducer and the job log.
        var state = DashboardState()
        var log = JobLogState()
        log.reset(jobID: job.id)
        log.applySnapshot(JobOutput(text: ""))
        for event in events {
            state.reduce(.event(event))
            log.appendLive(event)
        }
        XCTAssertTrue(state.activity.allSatisfy { !$0.isOutput })
        XCTAssertEqual(log.document.lines.filter { $0.info.gutter == .passed }.count, 8, "8 passing tests, seen live")
        XCTAssertTrue(log.document.lines.contains { $0.info.kind == .testSucceeded })

        // The streamed lines equal what the backend stored (this is what makes snapshot+live merging sound).
        let stored = LogDocument(text: try await client.output(jobID: job.id).text)
        XCTAssertEqual(log.document.lines.map(\.text), stored.lines.map(\.text))
    }

    func testStreamStopsWhenTheConsumerCancels() async throws {
        let client = APIClient(baseURL: try backendURL())
        let stream = EventStreamClient(api: client, connector: URLSessionWebSocketConnector())
        let connected = Box()
        let task = Task {
            for await signal in stream.signals(EventStreamOptions(replay: 0)) { connected.add(signal) }
        }
        for _ in 0..<100 where !connected.connected { try await Task.sleep(nanoseconds: 50_000_000) }
        XCTAssertTrue(connected.connected)
        task.cancel()
        _ = try await withTimeout(10) { await task.value; return true }
    }

    func testAnUnreachableBackendIsReportedAndRetried() async throws {
        let port = try XCTUnwrap(PortProbe.reserveFreePort())
        let client = APIClient(baseURL: URL(string: "http://127.0.0.1:\(port)")!)
        do {
            _ = try await client.health()
            XCTFail("nothing listens there")
        } catch let error as APIError {
            XCTAssertTrue(error.isConnectivity)
            XCTAssertEqual(error.message, "Cannot reach the backend at 127.0.0.1:\(port). Is it running?")
        }

        let sleeps = SleepRecorder()
        let stream = EventStreamClient(api: client, connector: URLSessionWebSocketConnector(), backoff: ReconnectBackoff(initialDelay: 0.01, maxDelay: 0.05, jitter: 0), sleep: sleeps.sleep)
        let signals = try await withTimeout(15) {
            var disconnects = 0
            return await collect(stream.signals()) { signal in
                if case .disconnected = signal { disconnects += 1 }
                return disconnects >= 2
            }
        }
        let disconnects = signals.compactMap { signal -> (String, TimeInterval)? in if case .disconnected(let reason, let retry) = signal { return (reason, retry) } else { return nil } }
        XCTAssertEqual(disconnects.count, 2)
        XCTAssertEqual(disconnects[0].1, 0.01, accuracy: 0.0001)
        XCTAssertEqual(disconnects[1].1, 0.02, accuracy: 0.0001)
        XCTAssertFalse(signals.contains(.connected))
    }

    func testTokenProtectedBackend() async throws {
        guard let text = ProcessInfo.processInfo.environment["IOSLAB_TEST_TOKEN_BACKEND_URL"], let url = URL(string: text),
              let token = ProcessInfo.processInfo.environment["IOSLAB_TEST_TOKEN"] else {
            throw XCTSkip("set IOSLAB_TEST_TOKEN_BACKEND_URL and IOSLAB_TEST_TOKEN to run the token tests")
        }
        // /health stays open
        let anonymous = APIClient(baseURL: url)
        _ = try await anonymous.health()
        do {
            _ = try await anonymous.devices()
            XCTFail("expected 401")
        } catch let error as APIError {
            XCTAssertTrue(error.isUnauthorized)
            XCTAssertTrue(error.message.contains("API token"))
        }
        let wrong = APIClient(baseURL: url, token: "wrong")
        do { _ = try await wrong.devices(); XCTFail("expected 401") } catch let error as APIError { XCTAssertTrue(error.isUnauthorized) }

        let authorised = APIClient(baseURL: url, token: token)
        _ = try await authorised.devices()

        // The WebSocket authenticates with the same header.
        let stream = EventStreamClient(api: authorised, connector: URLSessionWebSocketConnector())
        let signals = try await withTimeout(15) { await collect(stream.signals(EventStreamOptions(replay: 0))) { $0 == .connected } }
        XCTAssertTrue(signals.contains(.connected))
    }
}

private final class Box: @unchecked Sendable {
    private let lock = NSLock()
    private var all: [StreamSignal] = []

    func add(_ signal: StreamSignal) { lock.withLock { all.append(signal) } }
    var signals: [StreamSignal] { lock.withLock { all } }
    var connected: Bool { lock.withLock { all.contains(.connected) } }
    var sawOutput: Bool {
        lock.withLock { all.contains { if case .event(let e) = $0 { return e.isOutput } else { return false } } }
    }
}
