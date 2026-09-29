import Foundation

/// What is listening on a URL.
public enum ProbeResult: Equatable, Sendable {
    /// A MobileLab backend answered `/health`.
    case backend(Health)
    /// Something answered, but not a MobileLab backend (or the port is bound and silent).
    case occupied
    /// Nothing is there.
    case nothing
}

/// What the app should do about the backend when it starts.
public enum BackendPlan: Equatable, Sendable {
    /// Talk to a backend that is already running (started by the user, the CLI, or a previous app run).
    case attach(URL, reason: String)
    /// Start the bundled backend on this port.
    case launch(script: String, port: Int)
    /// There is no way to get a backend: the reason is shown in the UI.
    case unavailable(reason: String)
}

public struct BackendPlanner: Sendable {
    public static let defaultPort = 4000

    public var preferredURL: URL?
    public var bundledScript: String?
    public var defaultPort: Int
    public var probe: @Sendable (URL) async -> ProbeResult
    public var portIsFree: @Sendable (Int) -> Bool
    public var findFreePort: @Sendable () -> Int?

    public init(
        preferredURL: URL?, bundledScript: String?, defaultPort: Int = BackendPlanner.defaultPort,
        probe: @escaping @Sendable (URL) async -> ProbeResult, portIsFree: @escaping @Sendable (Int) -> Bool, findFreePort: @escaping @Sendable () -> Int?
    ) {
        self.preferredURL = preferredURL
        self.bundledScript = bundledScript
        self.defaultPort = defaultPort
        self.probe = probe
        self.portIsFree = portIsFree
        self.findFreePort = findFreePort
    }

    public static func localURL(port: Int) -> URL { URL(string: "http://127.0.0.1:\(port)")! }

    public func plan() async -> BackendPlan {
        // An address the person chose (Settings, or IOSLAB_BACKEND_URL) is never second-guessed and nothing is launched.
        if let preferredURL {
            return .attach(preferredURL, reason: "Using the backend address from Settings.")
        }

        let defaultURL = Self.localURL(port: defaultPort)
        guard let bundledScript else {
            return .attach(defaultURL, reason: "This build has no bundled backend. Start one yourself, or set its address in Settings.")
        }

        switch await probe(defaultURL) {
        case .backend:
            return .attach(defaultURL, reason: "A MobileLab backend is already running on port \(defaultPort).")
        case .nothing where portIsFree(defaultPort):
            return .launch(script: bundledScript, port: defaultPort)
        case .nothing, .occupied:
            guard let port = findFreePort() else {
                return .unavailable(reason: "Port \(defaultPort) is in use by another program and no other port could be reserved.")
            }
            return .launch(script: bundledScript, port: port)
        }
    }
}

/// How the last launch attempt went, for the UI.
public enum RuntimeStatus: Equatable, Sendable {
    case idle
    /// Talking to a backend somebody else started.
    case external(URL)
    case starting(port: Int)
    case running(port: Int)
    case unavailable(String)
    /// The bundled backend exited on its own. `tail` is its last output.
    case exited(code: Int32, tail: String)

    public var isProblem: Bool {
        switch self {
        case .unavailable, .exited: return true
        default: return false
        }
    }

    /// One sentence for the Welcome screen and Settings.
    public var message: String {
        switch self {
        case .idle: return "The backend has not been started."
        case .external(let url): return "Connected to the backend at \(url.absoluteString)."
        case .starting(let port): return "Starting the bundled backend on port \(port)\u{2026}"
        case .running(let port): return "The bundled backend is running on port \(port)."
        case .unavailable(let reason): return reason
        case .exited(let code, let tail):
            let hint = RuntimeStatus.explain(exitCode: code, output: tail)
            return hint
        }
    }

    /// Turns the exit code and the last output of the backend into a sentence a person can act on.
    public static func explain(exitCode code: Int32, output: String) -> String {
        let lowered = output.lowercased()
        if code == 127 || lowered.contains("node: no such file") || lowered.contains("command not found") {
            return "The bundled backend needs Node.js 20 or newer, and it was not found. Install it (for example with Homebrew: brew install node) and relaunch."
        }
        if lowered.contains("eaddrinuse") || lowered.contains("address already in use") {
            return "The backend could not start because its port is in use. Quit the other program, or set a different backend address in Settings."
        }
        let lastLine = output.split(separator: "\n").last.map(String.init)?.trimmingCharacters(in: .whitespaces) ?? ""
        let suffix = lastLine.isEmpty ? "" : " Last output: \(lastLine)"
        return "The bundled backend stopped (exit code \(code)).\(suffix)"
    }
}
