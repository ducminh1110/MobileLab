import XCTest
@testable import IOSLabDashboardCore

/// Records requests and answers from a queue of canned responses.
final class FakeTransport: HTTPTransport, @unchecked Sendable {
    enum Reply { case response(HTTPResponse), failure(Error) }

    private let lock = NSLock()
    private var replies: [Reply] = []
    private(set) var requests: [HTTPRequest] = []

    init(_ replies: [Reply] = []) { self.replies = replies }

    func enqueue(_ reply: Reply) {
        lock.lock(); defer { lock.unlock() }
        replies.append(reply)
    }

    func enqueue(json: String, status: Int = 200) { enqueue(.response(HTTPResponse(statusCode: status, body: Data(json.utf8)))) }

    func enqueueFixture(_ name: String, status: Int = 200) throws {
        enqueue(.response(HTTPResponse(statusCode: status, body: try Fixture.data(name))))
    }

    var last: HTTPRequest? {
        lock.lock(); defer { lock.unlock() }
        return requests.last
    }

    func send(_ request: HTTPRequest) async throws -> HTTPResponse {
        let reply: Reply? = lock.withLock {
            requests.append(request)
            return replies.isEmpty ? nil : replies.removeFirst()
        }
        switch reply {
        case .response(let response): return response
        case .failure(let error): throw error
        case nil: return HTTPResponse(statusCode: 500, body: Data(#"{"error":"internal_error","message":"no reply queued"}"#.utf8))
        }
    }
}

final class APIClientTests: XCTestCase {
    private func client(token: String? = nil, base: String = "http://127.0.0.1:4000", transport: FakeTransport) -> APIClient {
        APIClient(baseURL: URL(string: base)!, token: token, transport: transport)
    }

    func testGetDevicesDecodesRealResponseAndBuildsTheRightRequest() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("devices.json")
        let response = try await client(transport: transport).devices()
        XCTAssertEqual(response.items.count, 1)
        let request = try XCTUnwrap(transport.last)
        XCTAssertEqual(request.method, "GET")
        XCTAssertEqual(request.url.absoluteString, "http://127.0.0.1:4000/devices")
        XCTAssertEqual(request.headers["Accept"], "application/json")
        XCTAssertNil(request.headers["Authorization"])
        XCTAssertNil(request.body)
    }

    func testBearerTokenIsSentInAHeaderNotInTheURL() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("health.json")
        _ = try await client(token: "  secret-token \n", transport: transport).health()
        let request = try XCTUnwrap(transport.last)
        XCTAssertEqual(request.headers["Authorization"], "Bearer secret-token")
        XCTAssertFalse(request.url.absoluteString.contains("secret"))
    }

    func testBaseURLWithTrailingSlashAndPath() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("health.json")
        _ = try await client(base: "http://host.local:5000/", transport: transport).health()
        XCTAssertEqual(transport.last?.url.absoluteString, "http://host.local:5000/health")
    }

    func testJobsQueryParameters() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("tests.json")
        _ = try await client(transport: transport).jobs(status: .failed, runID: "run 1", limit: 25)
        let url = try XCTUnwrap(transport.last?.url)
        let items = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        XCTAssertEqual(url.path, "/tests")
        XCTAssertEqual(items.first { $0.name == "status" }?.value, "failed")
        XCTAssertEqual(items.first { $0.name == "runId" }?.value, "run 1")
        XCTAssertEqual(items.first { $0.name == "limit" }?.value, "25")
    }

    func testPathSegmentsAreEncoded() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("results-fail.json")
        _ = try await client(transport: transport).results(jobID: "../secret?x=1")
        XCTAssertEqual(transport.last?.url.absoluteString, "http://127.0.0.1:4000/tests/..%2Fsecret%3Fx%3D1/results")
        XCTAssertEqual(APIClient.segment("a b/c"), "a%20b%2Fc")
        XCTAssertEqual(APIClient.segment("3fa85f64-5717-4562-b3fc-2c963f66afa6"), "3fa85f64-5717-4562-b3fc-2c963f66afa6")
    }

    func testRunTestBodyOmitsNilFieldsAndUsesJSON() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("run-slow-queued.json", status: 202)
        let request = RunTestRequest(testTarget: "DemoApp", requiredRuntime: "com.apple.CoreSimulator.SimRuntime.iOS-18-0", autoProvision: false)
        let job = try await client(transport: transport).runTest(request)
        XCTAssertEqual(job.testTarget, "SlowSuite")
        let sent = try XCTUnwrap(transport.last)
        XCTAssertEqual(sent.method, "POST")
        XCTAssertEqual(sent.url.path, "/tests/run")
        XCTAssertEqual(sent.headers["Content-Type"], "application/json")
        let body = try XCTUnwrap(sent.body)
        XCTAssertEqual(String(decoding: body, as: UTF8.self), #"{"autoProvision":false,"requiredRuntime":"com.apple.CoreSimulator.SimRuntime.iOS-18-0","testTarget":"DemoApp"}"#)
    }

    func testCreateRunSendsMatrix() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("run-skipped.json", status: 202)
        let response = try await client(transport: transport).createRun(CreateRunRequest(testTarget: "DemoApp", runtimes: ["17.5"], models: ["iPhone 15", "iPhone 16 Pro"], maxParallel: 2))
        XCTAssertEqual(response.skipped.count, 1)
        let body = String(decoding: try XCTUnwrap(transport.last?.body), as: UTF8.self)
        XCTAssertEqual(body, #"{"maxParallel":2,"models":["iPhone 15","iPhone 16 Pro"],"runtimes":["17.5"],"testTarget":"DemoApp"}"#)
        XCTAssertEqual(transport.last?.url.path, "/runs")
    }

    func testActionEndpoints() async throws {
        let transport = FakeTransport()
        let c = client(transport: transport)

        try transport.enqueueFixture("device-spawn.json"); _ = try await c.bootDevice(id: "d1")
        XCTAssertEqual(transport.last?.url.path, "/devices/d1/boot")
        XCTAssertEqual(String(decoding: transport.last?.body ?? Data(), as: UTF8.self), "{}")

        try transport.enqueueFixture("device-spawn.json"); _ = try await c.shutdownDevice(id: "d1")
        XCTAssertEqual(transport.last?.url.path, "/devices/d1/shutdown")

        try transport.enqueueFixture("job-cancelled.json"); _ = try await c.cancel(jobID: "j1")
        XCTAssertEqual(transport.last?.url.path, "/tests/j1/cancel")
        XCTAssertEqual(transport.last?.method, "POST")

        try transport.enqueueFixture("run-slow-queued.json"); _ = try await c.rerun(jobID: "j1")
        XCTAssertEqual(transport.last?.url.path, "/tests/j1/rerun")

        transport.enqueue(json: #"{"run":{"id":"r","scheme":"S","jobIds":[],"createdAt":"2026-09-29T16:10:02Z","status":"cancelled","counts":{}}}"#)
        let run = try await c.cancel(runID: "r1")
        XCTAssertEqual(run.status, .cancelled)
        XCTAssertEqual(transport.last?.url.path, "/runs/r1/cancel")

        try transport.enqueueFixture("devices-sync.json"); _ = try await c.syncDevices()
        XCTAssertEqual(transport.last?.url.path, "/devices/sync")

        try transport.enqueueFixture("cleanup.json"); let cleaned = try await c.cleanup(days: 7)
        XCTAssertEqual(cleaned.jobsRemoved, 0)
        XCTAssertEqual(String(decoding: transport.last?.body ?? Data(), as: UTF8.self), #"{"days":7}"#)
    }

    func testDeleteAcceptsNoContent() async throws {
        let transport = FakeTransport([.response(HTTPResponse(statusCode: 204))])
        try await client(transport: transport).deleteDevice(id: "d1")
        XCTAssertEqual(transport.last?.method, "DELETE")
        XCTAssertEqual(transport.last?.url.path, "/devices/d1")
    }

    func testSpawnDeviceRequest() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("device-spawn.json")
        let device = try await client(transport: transport).spawnDevice(SpawnDeviceRequest(name: "iPhone 15 Test", runtime: "18.0", modelId: "iPhone 15", wait: false))
        XCTAssertEqual(device.name, "iPhone 15 Test")
        XCTAssertEqual(String(decoding: transport.last?.body ?? Data(), as: UTF8.self), #"{"modelId":"iPhone 15","name":"iPhone 15 Test","runtime":"18.0","wait":false}"#)
        XCTAssertGreaterThanOrEqual(transport.last?.timeout ?? 0, 60, "spawning waits for the simulator to boot")
    }

    func testScreenshotAndJUnitReturnRawBodies() async throws {
        let transport = FakeTransport([.response(HTTPResponse(statusCode: 200, body: try Fixture.data("screenshot.png"))), .response(HTTPResponse(statusCode: 200, body: try Fixture.data("junit-fail.xml")))])
        let c = client(transport: transport)
        let png = try await c.screenshot(deviceID: "d1")
        XCTAssertEqual(png.prefix(4), Data([0x89, 0x50, 0x4E, 0x47]))
        let xml = try await c.junit(jobID: "j1")
        XCTAssertTrue(xml.hasPrefix("<?xml"))
        XCTAssertTrue(xml.contains("testInvalidPassword"))
    }

    func testOutputTailQuery() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("output-fail.json")
        let output = try await client(transport: transport).output(jobID: "j1", tailBytes: 65536)
        XCTAssertEqual(transport.last?.url.query, "tail=65536")
        XCTAssertTrue(output.text.contains("Test Case"))
    }

    func testEventsQuery() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("events-job.json")
        let response = try await client(transport: transport).events(jobID: "j1", limit: 50)
        XCTAssertEqual(URLComponents(url: transport.last!.url, resolvingAgainstBaseURL: false)?.queryItems?.map(\.name), ["jobId", "limit"])
        XCTAssertFalse(response.items.isEmpty)
    }

    // MARK: Errors

    func testServerMessageIsSurfaced() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("error-409.json", status: 409)
        do {
            _ = try await client(transport: transport).cancel(jobID: "j1")
            XCTFail("expected an error")
        } catch let error as APIError {
            XCTAssertEqual(error, .server(status: 409, code: "request_failed", message: "Job is already cancelled."))
            XCTAssertEqual(error.message, "Job is already cancelled.")
            XCTAssertEqual(error.statusCode, 409)
            XCTAssertFalse(error.isConnectivity)
        }
    }

    func testValidationErrorKeepsTheServersWording() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("error-400.json", status: 400)
        do {
            _ = try await client(transport: transport).runTest(RunTestRequest(testTarget: ""))
            XCTFail("expected an error")
        } catch let error as APIError {
            XCTAssertEqual(error.message, "Invalid request: testTarget: testTarget (the Xcode scheme) is required")
        }
    }

    func testCapacityErrorIs429() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("error-429.json", status: 429)
        do {
            _ = try await client(transport: transport).spawnDevice(SpawnDeviceRequest())
            XCTFail("expected an error")
        } catch let error as APIError {
            XCTAssertEqual(error.statusCode, 429)
            XCTAssertTrue(error.message.hasPrefix("Capacity exceeded: 4 of 4 units in use"))
        }
    }

    func testUnauthorizedIsItsOwnCase() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("error-401.json", status: 401)
        do {
            _ = try await client(transport: transport).devices()
            XCTFail("expected an error")
        } catch let error as APIError {
            XCTAssertTrue(error.isUnauthorized)
            XCTAssertEqual(error.statusCode, 401)
            XCTAssertTrue(error.message.contains("Bearer <token>"))
        }
    }

    func testNotFound() async throws {
        let transport = FakeTransport()
        try transport.enqueueFixture("error-404.json", status: 404)
        do {
            _ = try await client(transport: transport).job(id: "nope")
            XCTFail("expected an error")
        } catch let error as APIError {
            XCTAssertEqual(error, .server(status: 404, code: "not_found", message: "Job not found: does-not-exist"))
        }
    }

    func testNonJSONErrorBodyStillProducesAReadableMessage() async throws {
        let transport = FakeTransport([.response(HTTPResponse(statusCode: 502, body: Data("Bad Gateway".utf8))), .response(HTTPResponse(statusCode: 503, body: Data()))])
        let c = client(transport: transport)
        do { _ = try await c.health(); XCTFail("expected an error") } catch let error as APIError { XCTAssertEqual(error.message, "HTTP 502: Bad Gateway") }
        do { _ = try await c.health(); XCTFail("expected an error") } catch let error as APIError { XCTAssertEqual(error.message, "The backend answered HTTP 503.") }
    }

    func testConnectionFailuresBecomeUnreachable() async throws {
        let transport = FakeTransport([.failure(URLError(.cannotConnectToHost)), .failure(URLError(.timedOut)), .failure(URLError(.notConnectedToInternet))])
        let c = client(transport: transport)
        do { _ = try await c.health(); XCTFail("expected an error") } catch let error as APIError {
            XCTAssertEqual(error, .unreachable("Cannot reach the backend at 127.0.0.1:4000. Is it running?"))
            XCTAssertTrue(error.isConnectivity)
        }
        do { _ = try await c.health(); XCTFail("expected an error") } catch let error as APIError {
            XCTAssertEqual(error.message, "The backend at 127.0.0.1:4000 did not answer in time.")
        }
        do { _ = try await c.health(); XCTFail("expected an error") } catch let error as APIError {
            XCTAssertTrue(error.isConnectivity)
        }
    }

    func testCancellationIsNotReportedAsAnError() async throws {
        let transport = FakeTransport([.failure(URLError(.cancelled))])
        do {
            _ = try await client(transport: transport).health()
            XCTFail("expected cancellation")
        } catch is CancellationError {
            // expected: a cancelled refresh must not flash an error banner
        }
    }

    func testUnexpectedShapeIsADecodingErrorThatNamesTheField() async throws {
        let transport = FakeTransport()
        transport.enqueue(json: #"{"items":[{"id":"d1"}]}"#)
        do {
            _ = try await client(transport: transport).devices()
            XCTFail("expected an error")
        } catch let error as APIError {
            guard case .decoding(let text) = error else { return XCTFail("wrong case \(error)") }
            XCTAssertTrue(text.contains("/devices"), text)
            XCTAssertTrue(text.contains("name"), text)
        }
    }

    // MARK: URLs

    func testWebSocketURLMirrorsTheScheme() {
        let http = APIClient(baseURL: URL(string: "http://127.0.0.1:4000")!, transport: FakeTransport())
        XCTAssertEqual(http.webSocketURL(query: [URLQueryItem(name: "replay", value: "100")])?.absoluteString, "ws://127.0.0.1:4000/ws/events?replay=100")
        let https = APIClient(baseURL: URL(string: "https://lab.example.com")!, transport: FakeTransport())
        XCTAssertEqual(https.webSocketURL()?.absoluteString, "wss://lab.example.com/ws/events")
    }

    func testDownloadURLCarriesTheTokenBecauseBrowsersCannotSendHeaders() throws {
        let artifact = try XCTUnwrap(Fixture.decode(ArtifactsResponse.self, "artifacts-pass").items.first)
        let c = APIClient(baseURL: URL(string: "http://127.0.0.1:4000")!, token: "tok", transport: FakeTransport())
        let url = try XCTUnwrap(c.downloadURL(for: artifact))
        XCTAssertTrue(url.absoluteString.hasSuffix("/download?token=tok"), url.absoluteString)
        let plain = APIClient(baseURL: URL(string: "http://127.0.0.1:4000")!, transport: FakeTransport())
        XCTAssertFalse(try XCTUnwrap(plain.downloadURL(for: artifact)).absoluteString.contains("token"))
    }

    func testParseBaseURL() {
        XCTAssertEqual(APIConfiguration.parseBaseURL("127.0.0.1:4000")?.absoluteString, "http://127.0.0.1:4000")
        XCTAssertEqual(APIConfiguration.parseBaseURL(" localhost:4000/ ")?.absoluteString, "http://localhost:4000")
        XCTAssertEqual(APIConfiguration.parseBaseURL("https://lab.example.com")?.absoluteString, "https://lab.example.com")
        XCTAssertNil(APIConfiguration.parseBaseURL(""))
        XCTAssertNil(APIConfiguration.parseBaseURL("ftp://host"))
        XCTAssertNil(APIConfiguration.parseBaseURL("http://"))
        XCTAssertNil(APIConfiguration.parseBaseURL("not a url"))
    }

    func testEmptyTokenIsDropped() {
        XCTAssertNil(APIConfiguration(baseURL: URL(string: "http://h")!, token: "   ").token)
        XCTAssertEqual(APIConfiguration(baseURL: URL(string: "http://h")!, token: " a ").token, "a")
    }
}
