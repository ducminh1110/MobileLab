import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// Everything that can go wrong talking to the backend, with the text the UI shows.
public enum APIError: Error, Equatable, Sendable, LocalizedError {
    case invalidURL(String)
    /// The request never got an answer: nothing listens there, a timeout, no network.
    case unreachable(String)
    /// 401: the backend wants an API token.
    case unauthorized(String)
    /// The backend answered with an error status. `message` is the server's own text.
    case server(status: Int, code: String?, message: String)
    /// The answer was not what the API documents.
    case decoding(String)

    /// Text for an inline error or a toast: the server's message when there is one.
    public var message: String {
        switch self {
        case .invalidURL(let text): return "Not a valid backend address: \(text)"
        case .unreachable(let text): return text
        case .unauthorized(let text): return text
        case .server(_, _, let text): return text
        case .decoding(let text): return "Unexpected answer from the backend: \(text)"
        }
    }

    public var errorDescription: String? { message }

    public var isUnauthorized: Bool {
        if case .unauthorized = self { return true }
        return false
    }

    /// True when the backend could not be reached at all (as opposed to answering with an error).
    public var isConnectivity: Bool {
        if case .unreachable = self { return true }
        return false
    }

    public var statusCode: Int? {
        switch self {
        case .unauthorized: return 401
        case .server(let status, _, _): return status
        default: return nil
        }
    }

    // MARK: Mapping

    struct ServerErrorBody: Decodable {
        var error: String?
        var message: String?
    }

    /// Turns a non-2xx answer into an error, preferring the `{ error, message }` body the backend sends.
    static func from(response: HTTPResponse) -> APIError {
        let body = try? JSONDecoder().decode(ServerErrorBody.self, from: response.body)
        var message = body?.message?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if message.isEmpty {
            let text = String(data: response.body.prefix(300), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            message = text.isEmpty ? "The backend answered HTTP \(response.statusCode)." : "HTTP \(response.statusCode): \(text)"
        }
        if response.statusCode == 401 { return .unauthorized(message) }
        return .server(status: response.statusCode, code: body?.error, message: message)
    }

    /// Turns a transport failure into something a person can act on.
    static func from(transportError error: Error, baseURL: URL) -> APIError {
        if let api = error as? APIError { return api }
        let place = baseURL.port.map { "\(baseURL.host ?? baseURL.absoluteString):\($0)" } ?? (baseURL.host ?? baseURL.absoluteString)
        if let urlError = error as? URLError {
            switch urlError.code {
            case .cannotConnectToHost, .networkConnectionLost, .notConnectedToInternet, .cannotFindHost, .dnsLookupFailed:
                return .unreachable("Cannot reach the backend at \(place). Is it running?")
            case .timedOut:
                return .unreachable("The backend at \(place) did not answer in time.")
            case .secureConnectionFailed, .serverCertificateUntrusted, .serverCertificateHasBadDate, .serverCertificateHasUnknownRoot, .clientCertificateRejected:
                return .unreachable("Could not make a secure connection to \(place): \(urlError.localizedDescription)")
            case .appTransportSecurityRequiresSecureConnection:
                return .unreachable("macOS blocks plain HTTP to \(place). Use https, an IP address, or a .local host name.")
            default:
                return .unreachable("Could not talk to the backend at \(place): \(urlError.localizedDescription)")
            }
        }
        return .unreachable("Could not talk to the backend at \(place): \(error.localizedDescription)")
    }
}
