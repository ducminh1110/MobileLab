import Foundation
#if canImport(Darwin)
import Darwin
#elseif canImport(Glibc)
import Glibc
#endif

/// Asks the operating system about loopback TCP ports with plain BSD sockets, which behave the same on macOS
/// and Linux. Used to avoid starting the bundled backend on a port that something else already holds.
public enum PortProbe {
    private static func makeSocket() -> Int32 {
        #if canImport(Darwin)
        return socket(AF_INET, SOCK_STREAM, 0)
        #else
        return socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
        #endif
    }

    private static func loopbackAddress(port: UInt16) -> sockaddr_in {
        var address = sockaddr_in()
        #if canImport(Darwin)
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        #endif
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = port.bigEndian
        address.sin_addr.s_addr = inet_addr("127.0.0.1")
        return address
    }

    /// True when a listener could bind `127.0.0.1:port` right now.
    public static func isFree(port: Int) -> Bool {
        guard (1...65_535).contains(port) else { return false }
        let fd = makeSocket()
        guard fd >= 0 else { return false }
        defer { close(fd) }

        var address = loopbackAddress(port: UInt16(port))
        let result = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        return result == 0
    }

    /// A port the system hands out for `127.0.0.1` (bind to port 0, read back what was assigned).
    /// There is a short window before the caller binds it in which another program could take it.
    public static func reserveFreePort() -> Int? {
        let fd = makeSocket()
        guard fd >= 0 else { return nil }
        defer { close(fd) }

        var address = loopbackAddress(port: 0)
        let bound = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bound == 0 else { return nil }

        var assigned = sockaddr_in()
        var length = socklen_t(MemoryLayout<sockaddr_in>.size)
        let named = withUnsafeMutablePointer(to: &assigned) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                getsockname(fd, $0, &length)
            }
        }
        guard named == 0 else { return nil }
        let port = Int(UInt16(bigEndian: assigned.sin_port))
        return port > 0 ? port : nil
    }
}
