import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if canImport(IOSLabDashboardCore)
import IOSLabDashboardCore
#endif

/// What `BackendRuntime.start` decided and achieved.
struct BackendOutcome: Equatable, Sendable {
    /// Where to connect; nil when there is no way to get a backend.
    var url: URL?
    var status: RuntimeStatus
    /// One sentence for the Welcome editor and Settings.
    var note: String
}

/// Starts and stops the backend that ships inside the app (Contents/Resources/backend-runtime/start-backend.sh).
///
/// The decision (attach to a running backend, launch the bundled one, or give up) is `BackendPlanner`'s, so it is
/// unit tested in the Core package. This class only does the process work: find Node, spawn, watch, terminate.
final class BackendRuntime: @unchecked Sendable {
    private let lock = NSLock()
    private var process: Process?
    private var tail = ""
    private var stopping = false

    /// Called (on an arbitrary thread) when the bundled backend exits without being asked to.
    var onExit: (@Sendable (Int32, String) -> Void)? {
        get { lock.lock(); defer { lock.unlock() }; return exitHandler }
        set { lock.lock(); exitHandler = newValue; lock.unlock() }
    }
    private var exitHandler: (@Sendable (Int32, String) -> Void)?

    static var bundledScriptPath: String? {
        Bundle.main.path(forResource: "start-backend", ofType: "sh", inDirectory: "backend-runtime")
    }

    /// A GUI app does not inherit the shell's PATH, so the usual install locations of Node are added.
    static func searchPath(base: String? = ProcessInfo.processInfo.environment["PATH"]) -> String {
        let extras = ["/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin", "/usr/bin", "/bin",
                      (NSHomeDirectory() as NSString).appendingPathComponent(".volta/bin"),
                      (NSHomeDirectory() as NSString).appendingPathComponent(".nvm/current/bin")]
        var parts = (base ?? "").split(separator: ":").map(String.init)
        for extra in extras where !parts.contains(extra) { parts.append(extra) }
        return parts.joined(separator: ":")
    }

    static func findNode(path: String = searchPath()) -> String? {
        for dir in path.split(separator: ":") {
            let candidate = (String(dir) as NSString).appendingPathComponent("node")
            if FileManager.default.isExecutableFile(atPath: candidate) { return candidate }
        }
        return nil
    }

    // MARK: Start

    func start(preferredURL: URL?) async -> BackendOutcome {
        let planner = BackendPlanner(
            preferredURL: preferredURL,
            bundledScript: Self.bundledScriptPath,
            probe: { url in await BackendRuntime.probe(url) },
            portIsFree: { PortProbe.isFree(port: $0) },
            findFreePort: { PortProbe.reserveFreePort() }
        )
        switch await planner.plan() {
        case .attach(let url, let reason):
            return BackendOutcome(url: url, status: .external(url), note: reason)
        case .unavailable(let reason):
            return BackendOutcome(url: nil, status: .unavailable(reason), note: reason)
        case .launch(let script, let port):
            return await launch(script: script, port: port)
        }
    }

    private func launch(script: String, port: Int) async -> BackendOutcome {
        let path = Self.searchPath()
        guard Self.findNode(path: path) != nil else {
            let message = RuntimeStatus.explain(exitCode: 127, output: "")
            return BackendOutcome(url: nil, status: .unavailable(message), note: message)
        }
        guard FileManager.default.fileExists(atPath: script) else {
            let message = "The bundled backend is missing from the app (\(script))."
            return BackendOutcome(url: nil, status: .unavailable(message), note: message)
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/bash")
        process.arguments = [script]
        var environment = ProcessInfo.processInfo.environment
        environment["PORT"] = String(port)
        environment["HOST"] = "127.0.0.1"
        environment["PATH"] = path
        process.environment = environment
        process.standardInput = FileHandle.nullDevice

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty {
                handle.readabilityHandler = nil
                return
            }
            self?.append(String(decoding: data, as: UTF8.self))
        }
        process.terminationHandler = { [weak self] finished in
            self?.finished(finished)
        }

        register(process)

        do {
            try process.run()
        } catch {
            unregister()
            let message = "The bundled backend could not be started: \(error.localizedDescription)"
            return BackendOutcome(url: nil, status: .unavailable(message), note: message)
        }

        // Wait until /health answers, the process dies, or 30 seconds pass.
        let url = BackendPlanner.localURL(port: port)
        for _ in 0..<120 {
            if !process.isRunning {
                let (code, output) = snapshotExit(process)
                let message = RuntimeStatus.explain(exitCode: code, output: output)
                return BackendOutcome(url: nil, status: .exited(code: code, tail: output), note: message)
            }
            if case .backend = await Self.probe(url) {
                return BackendOutcome(url: url, status: .running(port: port), note: "The bundled backend is running on port \(port).")
            }
            try? await Task.sleep(nanoseconds: 250_000_000)
        }
        // Still running but not answering: keep going, the socket reconnect loop will find it when it is ready.
        return BackendOutcome(url: url, status: .starting(port: port), note: "The bundled backend is still starting on port \(port).")
    }

    static func probe(_ url: URL) async -> ProbeResult {
        let client = APIClient(configuration: APIConfiguration(baseURL: url), transport: URLSessionTransport(), timeout: 1.5)
        do {
            return .backend(try await client.health())
        } catch let error as APIError {
            if case .unreachable = error { return .nothing }
            return .occupied
        } catch {
            return .nothing
        }
    }

    // MARK: Stop

    /// Terminates the bundled backend if this app started it. Safe to call repeatedly.
    func stop() {
        lock.lock()
        stopping = true
        let running = process
        process = nil
        lock.unlock()
        guard let running, running.isRunning else { return }
        running.terminate()
    }

    var isRunning: Bool {
        lock.lock(); defer { lock.unlock() }
        return process?.isRunning ?? false
    }

    // MARK: Output

    private func register(_ process: Process) {
        lock.lock()
        tail = ""
        stopping = false
        self.process = process
        lock.unlock()
    }

    private func unregister() {
        lock.lock()
        process = nil
        lock.unlock()
    }

    private func append(_ text: String) {
        lock.lock()
        tail += text
        if tail.count > 4000 { tail = String(tail.suffix(4000)) }
        lock.unlock()
    }

    private func snapshotExit(_ process: Process) -> (Int32, String) {
        lock.lock(); defer { lock.unlock() }
        return (process.terminationStatus, tail)
    }

    private func finished(_ process: Process) {
        lock.lock()
        let wasStopping = stopping
        let output = tail
        let handler = exitHandler
        if self.process === process { self.process = nil }
        lock.unlock()
        if !wasStopping { handler?(process.terminationStatus, output) }
    }
}
