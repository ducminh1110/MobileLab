import XCTest
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif
@testable import IOSLabDashboardCore

final class RuntimeTests: XCTestCase {
    private func planner(preferred: URL? = nil, script: String? = "/App/backend-runtime/start-backend.sh", probe: ProbeResult = .nothing, free: Bool = true, freePort: Int? = 49_555) -> BackendPlanner {
        BackendPlanner(preferredURL: preferred, bundledScript: script, probe: { _ in probe }, portIsFree: { _ in free }, findFreePort: { freePort })
    }

    func testAnExplicitAddressIsAlwaysUsedAndNothingIsLaunched() async {
        let url = URL(string: "http://10.0.0.5:4000")!
        let plan = await planner(preferred: url, probe: .nothing).plan()
        XCTAssertEqual(plan, .attach(url, reason: "Using the backend address from Settings."))
    }

    func testWithoutABundledBackendItAttachesToTheDefaultAddress() async {
        let plan = await planner(script: nil).plan()
        guard case .attach(let url, let reason) = plan else { return XCTFail("expected attach") }
        XCTAssertEqual(url.absoluteString, "http://127.0.0.1:4000")
        XCTAssertTrue(reason.contains("no bundled backend"))
    }

    func testAnAlreadyRunningBackendIsReusedNotDuplicated() async {
        let plan = await planner(probe: .backend(Health(mode: .live))).plan()
        XCTAssertEqual(plan, .attach(URL(string: "http://127.0.0.1:4000")!, reason: "A MobileLab backend is already running on port 4000."))
    }

    func testAFreeDefaultPortIsUsed() async {
        let plan = await planner(probe: .nothing, free: true).plan()
        XCTAssertEqual(plan, .launch(script: "/App/backend-runtime/start-backend.sh", port: 4000))
    }

    func testAPortHeldByAnotherProgramMovesToAFreeOne() async {
        let occupied = await planner(probe: .occupied).plan()
        XCTAssertEqual(occupied, .launch(script: "/App/backend-runtime/start-backend.sh", port: 49_555))
        let silentButBound = await planner(probe: .nothing, free: false).plan()
        XCTAssertEqual(silentButBound, .launch(script: "/App/backend-runtime/start-backend.sh", port: 49_555), "silent but bound")
    }

    func testNoPortAtAllIsReported() async {
        let plan = await planner(probe: .occupied, freePort: nil).plan()
        guard case .unavailable(let reason) = plan else { return XCTFail("expected unavailable") }
        XCTAssertTrue(reason.contains("Port 4000 is in use"))
    }

    func testExplainingWhyTheBackendStopped() {
        XCTAssertTrue(RuntimeStatus.explain(exitCode: 127, output: "env: node: No such file or directory").contains("Node.js 20"))
        XCTAssertTrue(RuntimeStatus.explain(exitCode: 1, output: "Error: listen EADDRINUSE: address already in use 127.0.0.1:4000").contains("port is in use"))
        let generic = RuntimeStatus.explain(exitCode: 3, output: "starting\nInvalid configuration: PORT: expected a positive integer\n")
        XCTAssertEqual(generic, "The bundled backend stopped (exit code 3). Last output: Invalid configuration: PORT: expected a positive integer")
        XCTAssertEqual(RuntimeStatus.explain(exitCode: 1, output: ""), "The bundled backend stopped (exit code 1).")
    }

    func testRuntimeStatusMessages() {
        XCTAssertFalse(RuntimeStatus.idle.isProblem)
        XCTAssertFalse(RuntimeStatus.running(port: 4000).isProblem)
        XCTAssertTrue(RuntimeStatus.unavailable("x").isProblem)
        XCTAssertTrue(RuntimeStatus.exited(code: 1, tail: "").isProblem)
        XCTAssertEqual(RuntimeStatus.starting(port: 4001).message, "Starting the bundled backend on port 4001\u{2026}")
        XCTAssertEqual(RuntimeStatus.external(URL(string: "http://h:1")!).message, "Connected to the backend at http://h:1.")
    }

    // MARK: Ports (real sockets)

    func testPortProbeSeesABoundPort() throws {
        let port = try XCTUnwrap(PortProbe.reserveFreePort())
        XCTAssertGreaterThan(port, 1024)
        XCTAssertTrue(PortProbe.isFree(port: port), "reserving does not keep the port")

        let held = try Listener(port: port)
        XCTAssertFalse(PortProbe.isFree(port: port), "a listening socket holds the port")
        held.close()
        XCTAssertTrue(PortProbe.isFree(port: port))
    }

    func testPortProbeRejectsNonsense() {
        XCTAssertFalse(PortProbe.isFree(port: 0))
        XCTAssertFalse(PortProbe.isFree(port: 70_000))
        XCTAssertFalse(PortProbe.isFree(port: -1))
    }

    func testReservedPortsDiffer() throws {
        let ports = Set((0..<5).compactMap { _ in PortProbe.reserveFreePort() })
        XCTAssertGreaterThanOrEqual(ports.count, 2)
    }
}

/// A plain listening socket, to occupy a port for the test.
private final class Listener {
    private let fd: Int32

    init(port: Int) throws {
        #if canImport(Darwin)
        fd = Darwin.socket(AF_INET, SOCK_STREAM, 0)
        #else
        fd = Glibc.socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
        #endif
        guard fd >= 0 else { throw POSIXError(.EMFILE) }
        var address = sockaddr_in()
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = UInt16(port).bigEndian
        address.sin_addr.s_addr = inet_addr("127.0.0.1")
        let bound = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bound == 0, listen(fd, 1) == 0 else {
            close()
            throw POSIXError(.EADDRINUSE)
        }
    }

    func close() {
        #if canImport(Darwin)
        _ = Darwin.close(fd)
        #else
        _ = Glibc.close(fd)
        #endif
    }
}
