import Foundation

/// Where the backend is and how to authenticate.
public struct APIConfiguration: Equatable, Sendable {
    public var baseURL: URL
    public var token: String?

    public init(baseURL: URL, token: String? = nil) {
        self.baseURL = baseURL
        let trimmed = token?.trimmingCharacters(in: .whitespacesAndNewlines)
        self.token = (trimmed?.isEmpty ?? true) ? nil : trimmed
    }

    /// Accepts `127.0.0.1:4000`, `localhost:4000` and full URLs; anything else is nil.
    public static func parseBaseURL(_ text: String) -> URL? {
        var trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        if !trimmed.contains("://") { trimmed = "http://" + trimmed }
        while trimmed.hasSuffix("/") { trimmed.removeLast() }
        guard let url = URL(string: trimmed), let scheme = url.scheme?.lowercased(), scheme == "http" || scheme == "https",
              let host = url.host, !host.isEmpty else { return nil }
        return url
    }
}

/// Typed access to the MobileLab backend (see docs/api.md). Stateless and `Sendable`: share one instance.
public final class APIClient: Sendable {
    public let configuration: APIConfiguration
    private let transport: HTTPTransport
    private let defaultTimeout: TimeInterval

    public init(configuration: APIConfiguration, transport: HTTPTransport = URLSessionTransport(), timeout: TimeInterval = 20) {
        self.configuration = configuration
        self.transport = transport
        self.defaultTimeout = timeout
    }

    public convenience init(baseURL: URL, token: String? = nil, transport: HTTPTransport = URLSessionTransport()) {
        self.init(configuration: APIConfiguration(baseURL: baseURL, token: token), transport: transport)
    }

    public var baseURL: URL { configuration.baseURL }

    // MARK: URLs

    /// `path` starts with a slash and is already percent-encoded (use `Self.segment` for ids).
    public func url(path: String, query: [URLQueryItem] = [], includeToken: Bool = false) -> URL? {
        var text = configuration.baseURL.absoluteString
        while text.hasSuffix("/") { text.removeLast() }
        guard var components = URLComponents(string: text + path) else { return nil }
        var items = query
        if includeToken, let token = configuration.token { items.append(URLQueryItem(name: "token", value: token)) }
        if !items.isEmpty { components.queryItems = items }
        return components.url
    }

    /// A URL a browser or Finder can open, which cannot send headers: the token rides in the query string.
    public func downloadURL(for artifact: Artifact) -> URL? {
        guard let path = artifact.downloadUrl else { return nil }
        return url(path: path, includeToken: true)
    }

    /// Percent-encodes one path segment (an id) so it cannot add slashes or a query.
    public static func segment(_ value: String) -> String {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-._~")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }

    /// Where the event WebSocket lives (`ws://` or `wss://` mirroring the base URL).
    public func webSocketURL(path: String = "/ws/events", query: [URLQueryItem] = []) -> URL? {
        guard let http = url(path: path, query: query), var components = URLComponents(url: http, resolvingAgainstBaseURL: false) else { return nil }
        components.scheme = (components.scheme?.lowercased() == "https") ? "wss" : "ws"
        return components.url
    }

    public var authorizationHeaders: [String: String] {
        configuration.token.map { ["Authorization": "Bearer \($0)"] } ?? [:]
    }

    // MARK: System

    public func health() async throws -> Health { try await get("/health") }
    public func capabilities() async throws -> BackendCapabilities { try await get("/capabilities") }
    public func catalog() async throws -> Catalog { try await get("/catalog") }
    public func doctor() async throws -> DoctorReport { try await get("/doctor", timeout: 60) }
    public func metrics() async throws -> MetricsSummary { try await get("/metrics/summary") }
    public func cleanup(days: Int?) async throws -> CleanupResult {
        try await post("/maintenance/cleanup", body: CleanupBody(days: days))
    }

    // MARK: Devices

    public func devices() async throws -> DevicesResponse { try await get("/devices") }
    public func device(id: String) async throws -> Device { try await get("/devices/\(Self.segment(id))") }

    public func spawnDevice(_ request: SpawnDeviceRequest) async throws -> Device {
        try await post("/devices/spawn", body: request, timeout: 180)
    }

    public func bootDevice(id: String) async throws -> Device {
        try await post("/devices/\(Self.segment(id))/boot", body: EmptyBody(), timeout: 120)
    }

    public func shutdownDevice(id: String) async throws -> Device {
        try await post("/devices/\(Self.segment(id))/shutdown", body: EmptyBody(), timeout: 120)
    }

    public func deleteDevice(id: String) async throws {
        _ = try await send(method: "DELETE", path: "/devices/\(Self.segment(id))", body: nil, timeout: 120)
    }

    public func syncDevices() async throws -> SyncResult { try await post("/devices/sync", body: EmptyBody()) }

    /// PNG bytes of the running simulator's screen.
    public func screenshot(deviceID: String) async throws -> Data {
        try await send(method: "GET", path: "/devices/\(Self.segment(deviceID))/screenshot", body: nil, timeout: 30).body
    }

    // MARK: Jobs

    public func jobs(status: JobStatus? = nil, runID: String? = nil, limit: Int? = nil) async throws -> [TestJob] {
        var query: [URLQueryItem] = []
        if let status { query.append(URLQueryItem(name: "status", value: status.rawValue)) }
        if let runID { query.append(URLQueryItem(name: "runId", value: runID)) }
        if let limit { query.append(URLQueryItem(name: "limit", value: String(limit))) }
        let response: JobsResponse = try await get("/tests", query: query)
        return response.items
    }

    public func job(id: String) async throws -> TestJob { try await get("/tests/\(Self.segment(id))") }

    public func runTest(_ request: RunTestRequest) async throws -> TestJob {
        let envelope: JobEnvelope = try await post("/tests/run", body: request)
        return envelope.job
    }

    public func rerun(jobID: String) async throws -> TestJob {
        let envelope: JobEnvelope = try await post("/tests/\(Self.segment(jobID))/rerun", body: EmptyBody())
        return envelope.job
    }

    public func cancel(jobID: String) async throws -> TestJob {
        try await post("/tests/\(Self.segment(jobID))/cancel", body: EmptyBody())
    }

    public func results(jobID: String) async throws -> JobResults { try await get("/tests/\(Self.segment(jobID))/results") }

    public func output(jobID: String, tailBytes: Int? = nil) async throws -> JobOutput {
        let query = tailBytes.map { [URLQueryItem(name: "tail", value: String($0))] } ?? []
        return try await get("/tests/\(Self.segment(jobID))/output", query: query, timeout: 30)
    }

    public func junit(jobID: String) async throws -> String {
        let data = try await send(method: "GET", path: "/tests/\(Self.segment(jobID))/junit", body: nil, timeout: 30).body
        return String(decoding: data, as: UTF8.self)
    }

    public func artifacts(jobID: String) async throws -> [Artifact] {
        let response: ArtifactsResponse = try await get("/tests/\(Self.segment(jobID))/artifacts")
        return response.items
    }

    // MARK: Runs

    public func runs() async throws -> [TestRunView] {
        let response: RunsResponse = try await get("/runs")
        return response.items
    }

    public func run(id: String) async throws -> RunDetail { try await get("/runs/\(Self.segment(id))") }

    public func createRun(_ request: CreateRunRequest) async throws -> CreateRunResponse {
        try await post("/runs", body: request)
    }

    public func cancel(runID: String) async throws -> TestRunView {
        let envelope: RunEnvelope = try await post("/runs/\(Self.segment(runID))/cancel", body: EmptyBody())
        return envelope.run
    }

    // MARK: Events

    public func events(jobID: String? = nil, deviceID: String? = nil, runID: String? = nil, limit: Int? = nil) async throws -> EventsResponse {
        var query: [URLQueryItem] = []
        if let jobID { query.append(URLQueryItem(name: "jobId", value: jobID)) }
        if let deviceID { query.append(URLQueryItem(name: "deviceId", value: deviceID)) }
        if let runID { query.append(URLQueryItem(name: "runId", value: runID)) }
        if let limit { query.append(URLQueryItem(name: "limit", value: String(limit))) }
        return try await get("/events", query: query)
    }

    // MARK: Plumbing

    private struct EmptyBody: Encodable {}
    private struct CleanupBody: Encodable { var days: Int? }

    private func get<T: Decodable>(_ path: String, query: [URLQueryItem] = [], timeout: TimeInterval? = nil) async throws -> T {
        let response = try await send(method: "GET", path: path, query: query, body: nil, timeout: timeout)
        return try decode(response, path: path)
    }

    private func post<T: Decodable, B: Encodable>(_ path: String, body: B, timeout: TimeInterval? = nil) async throws -> T {
        let data: Data
        do {
            data = try BackendJSON.encoder().encode(body)
        } catch {
            throw APIError.decoding("could not encode the request for \(path): \(error)")
        }
        let response = try await send(method: "POST", path: path, body: data, timeout: timeout)
        return try decode(response, path: path)
    }

    private func decode<T: Decodable>(_ response: HTTPResponse, path: String) throws -> T {
        do {
            return try BackendJSON.decoder().decode(T.self, from: response.body)
        } catch let error as DecodingError {
            throw APIError.decoding("\(path): \(Self.describe(error))")
        } catch {
            throw APIError.decoding("\(path): \(error.localizedDescription)")
        }
    }

    private func send(method: String, path: String, query: [URLQueryItem] = [], body: Data?, timeout: TimeInterval?) async throws -> HTTPResponse {
        guard let url = url(path: path, query: query) else { throw APIError.invalidURL(configuration.baseURL.absoluteString + path) }
        var headers = authorizationHeaders
        headers["Accept"] = "application/json"
        if body != nil { headers["Content-Type"] = "application/json" }
        let request = HTTPRequest(method: method, url: url, headers: headers, body: body, timeout: timeout ?? defaultTimeout)

        let response: HTTPResponse
        do {
            response = try await transport.send(request)
        } catch is CancellationError {
            throw CancellationError()
        } catch let error as URLError where error.code == .cancelled {
            throw CancellationError()
        } catch {
            throw APIError.from(transportError: error, baseURL: configuration.baseURL)
        }
        guard (200..<300).contains(response.statusCode) else { throw APIError.from(response: response) }
        return response
    }

    static func describe(_ error: DecodingError) -> String {
        func location(_ context: DecodingError.Context) -> String {
            let path = context.codingPath.map { $0.intValue.map { "[\($0)]" } ?? $0.stringValue }.joined(separator: ".")
            return path.isEmpty ? "" : " at \(path)"
        }
        switch error {
        case .keyNotFound(let key, let context): return "missing \"\(key.stringValue)\"" + location(context)
        case .typeMismatch(_, let context): return "wrong type" + location(context)
        case .valueNotFound(_, let context): return "missing value" + location(context)
        case .dataCorrupted(let context): return "invalid data" + location(context) + " (\(context.debugDescription))"
        @unknown default: return error.localizedDescription
        }
    }
}
