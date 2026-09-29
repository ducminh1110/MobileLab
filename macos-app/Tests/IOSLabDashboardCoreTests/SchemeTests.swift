import XCTest
@testable import IOSLabDashboardCore

final class SchemeTests: XCTestCase {
    private let ios18 = "com.apple.CoreSimulator.SimRuntime.iOS-18-0"
    private let ios175 = "com.apple.CoreSimulator.SimRuntime.iOS-17-5"
    private let iphone15 = "com.apple.CoreSimulator.SimDeviceType.iPhone-15"
    private let ipad = "com.apple.CoreSimulator.SimDeviceType.iPad-10th-generation"

    private func scheme(_ name: String = "DemoApp") -> SchemeConfig { SchemeConfig(id: "s1", name: name) }

    // MARK: Schemes

    func testSchemeValidationMirrorsTheBackendsRules() {
        XCTAssertTrue(scheme().problems().isEmpty)
        XCTAssertEqual(scheme("  ").problems().count, 1)
        XCTAssertTrue(scheme("-list").problems().first?.contains("must not start") ?? false)
        var both = scheme()
        both.projectPath = "A.xcodeproj"
        both.workspacePath = "A.xcworkspace"
        XCTAssertTrue(both.problems().contains { $0.contains("either a project or a workspace") })
        var blankPaths = scheme()
        blankPaths.projectPath = " "
        blankPaths.workspacePath = "A.xcworkspace"
        XCTAssertTrue(blankPaths.problems().isEmpty, "a blank field is not a value")
        var retries = scheme()
        retries.maxRetries = 6
        XCTAssertFalse(retries.problems().isEmpty)
        var parallel = scheme()
        parallel.maxParallel = 0
        XCTAssertFalse(parallel.problems().isEmpty)
        var filters = scheme()
        filters.onlyTesting = ["-evil"]
        XCTAssertFalse(filters.problems().isEmpty)
        XCTAssertFalse(SchemeConfig(name: String(repeating: "x", count: 300)).problems().isEmpty)
    }

    func testParsingTheOnlyTestingField() {
        XCTAssertEqual(SchemeConfig.parseFilters("AppTests/LoginTests, AppTests/CheckoutTests\n\n  UITests  "), ["AppTests/LoginTests", "AppTests/CheckoutTests", "UITests"])
        XCTAssertEqual(SchemeConfig.parseFilters(""), [])
    }

    func testSchemeCodableRoundTripAndTolerance() throws {
        var original = scheme()
        original.workspacePath = "A.xcworkspace"
        original.onlyTesting = ["T"]
        original.maxRetries = 2
        original.maxParallel = 3
        original.autoProvision = false
        let decoded = try JSONDecoder().decode(SchemeConfig.self, from: JSONEncoder().encode(original))
        XCTAssertEqual(decoded, original)
        let minimal = try JSONDecoder().decode(SchemeConfig.self, from: Data(#"{"name":"Only"}"#.utf8))
        XCTAssertEqual(minimal.name, "Only")
        XCTAssertTrue(minimal.autoProvision)
        XCTAssertEqual(minimal.maxRetries, 0)
        XCTAssertFalse(minimal.id.isEmpty)
    }

    func testSchemeListMergesStoredAndHistory() throws {
        let stored = [SchemeConfig(id: "b", name: "Zeta"), SchemeConfig(id: "a", name: "Alpha")]
        let jobs = [
            makeJob("1", scheme: "Alpha", status: .completed, created: "2026-09-29T08:00:00Z"),
            makeJob("2", scheme: "FromHistory", status: .completed, created: "2026-09-29T09:00:00Z"),
            makeJob("3", scheme: "Older", status: .failed, created: "2026-09-28T09:00:00Z"),
            makeJob("4", scheme: "FromHistory", status: .failed, created: "2026-09-27T09:00:00Z")
        ]
        let list = SchemeList.merged(stored: stored, jobs: jobs)
        XCTAssertEqual(list.map(\.name), ["Alpha", "Zeta", "FromHistory", "Older"], "created ones first, then recent names, newest first, no duplicates")
        XCTAssertEqual(list.map(\.isRecent), [false, false, true, true])
        XCTAssertEqual(SchemeList.selected(id: "b", in: list, jobs: jobs)?.name, "Zeta")
        XCTAssertEqual(SchemeList.selected(id: "gone", in: list, jobs: jobs)?.name, "FromHistory", "falls back to the most recently used scheme")
        XCTAssertNil(SchemeList.selected(id: nil, in: [], jobs: []))
        XCTAssertEqual(SchemeList.merged(stored: [], jobs: []), [])
    }

    // MARK: Destinations

    func testDestinationMenuFromDevicesAndCatalog() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-ephemeral").items
        let catalog = try Fixture.decode(Catalog.self, "catalog")
        let menu = Destinations.menu(devices: devices, catalog: catalog)

        XCTAssertEqual(menu.booted.map(\.title), ["iPhone 15 Test", "iPhone 15 (iOS 17.5)"])
        XCTAssertEqual(menu.booted.first?.key, DestinationKey(runtime: ios18, model: iphone15))
        XCTAssertEqual(menu.onDemand.map { $0.runtime.name }, ["iOS 18.2", "iOS 18.0", "iOS 17.5"])
        XCTAssertEqual(menu.onDemand.map { $0.items.count }, [4, 4, 3], "iPhone 16 Pro is not offered on iOS 17.5")
        XCTAssertFalse(menu.onDemand[2].items.contains { $0.title == "iPhone 16 Pro" })
        XCTAssertTrue(menu.onDemand[0].items.contains { $0.isTablet })
    }

    func testStoppedDevicesAndVMsAreNotDestinations() {
        let devices = [
            Device(id: "1", name: "Stopped", runtime: ios18, modelId: iphone15, status: .stopped),
            Device(id: "2", name: "VM", runtime: "simulated", status: .ready, type: .vm, backend: .simulated, canRunTests: false),
            Device(id: "3", name: "Twin A", runtime: ios18, modelId: iphone15, status: .ready),
            Device(id: "4", name: "Twin B", runtime: ios18, modelId: iphone15, status: .busy)
        ]
        let menu = Destinations.menu(devices: devices, catalog: nil)
        XCTAssertEqual(menu.booted.map(\.title), ["Twin A"], "two booted devices of the same kind are one destination")
        XCTAssertTrue(menu.onDemand.isEmpty)
    }

    func testDestinationSummaryText() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices").items
        let catalog = try Fixture.decode(Catalog.self, "catalog")
        XCTAssertEqual(Destinations.summary(selected: [], devices: devices, catalog: catalog), "Any Available Simulator")
        XCTAssertEqual(Destinations.summary(selected: [.any], devices: devices, catalog: catalog), "Any Available Simulator")
        XCTAssertEqual(Destinations.summary(selected: [DestinationKey(runtime: ios18, model: iphone15)], devices: devices, catalog: catalog), "iPhone 15 (iOS 18.0)")
        XCTAssertEqual(Destinations.summary(selected: [DestinationKey(runtime: ios18, model: iphone15), DestinationKey(runtime: ios175, model: ipad)], devices: devices, catalog: catalog), "2 Destinations")
        XCTAssertEqual(Destinations.summary(selected: [DestinationKey(runtime: ios175, model: nil)], devices: devices, catalog: catalog), "Any Device (iOS 17.5)")
        XCTAssertEqual(Destinations.title(for: DestinationKey(runtime: ios175, model: ipad), devices: [], catalog: catalog), "iPad (10th generation) (iOS 17.5)")
    }

    // MARK: Run planning

    func testNoDestinationRunsOnAnySimulator() {
        let plans = RunPlanner.plan(scheme: scheme(), destinations: [])
        XCTAssertEqual(plans, [.single(RunTestRequest(testTarget: "DemoApp"))])
        XCTAssertEqual(RunPlanner.plan(scheme: scheme(), destinations: [.any]), plans)
    }

    func testOneDestinationIsOneJobPinnedToItsRuntimeAndModel() {
        let plans = RunPlanner.plan(scheme: scheme(), destinations: [DestinationKey(runtime: ios18, model: iphone15)])
        XCTAssertEqual(plans, [.single(RunTestRequest(testTarget: "DemoApp", requiredRuntime: ios18, requiredModelId: iphone15))])
    }

    func testTheSchemesSettingsGoIntoTheRequest() {
        var config = scheme("  DemoApp ")
        config.workspacePath = " App.xcworkspace "
        config.configuration = "Debug"
        config.onlyTesting = ["AppTests/LoginTests"]
        config.maxRetries = 2
        config.autoProvision = false
        guard case .single(let request)? = RunPlanner.plan(scheme: config, destinations: []).first else { return XCTFail("expected a single run") }
        XCTAssertEqual(request.testTarget, "DemoApp")
        XCTAssertEqual(request.workspacePath, "App.xcworkspace")
        XCTAssertNil(request.projectPath)
        XCTAssertEqual(request.configuration, "Debug")
        XCTAssertEqual(request.onlyTesting, ["AppTests/LoginTests"])
        XCTAssertEqual(request.maxRetries, 2)
        XCTAssertEqual(request.autoProvision, false)
    }

    func testAFullGridBecomesOneMatrixRun() {
        var config = scheme()
        config.maxParallel = 2
        let destinations = [
            DestinationKey(runtime: ios18, model: iphone15), DestinationKey(runtime: ios18, model: ipad),
            DestinationKey(runtime: ios175, model: iphone15), DestinationKey(runtime: ios175, model: ipad)
        ]
        let plans = RunPlanner.plan(scheme: config, destinations: destinations)
        XCTAssertEqual(plans.count, 1)
        guard case .matrix(let request) = plans[0] else { return XCTFail("expected a matrix run") }
        XCTAssertEqual(request.runtimes, [ios175, ios18].sorted())
        XCTAssertEqual(request.models, [ipad, iphone15].sorted())
        XCTAssertEqual(request.maxParallel, 2)
        XCTAssertEqual(request.name, "DemoApp")
    }

    func testTwoRuntimesOfOneDeviceTypeIsAMatrixToo() {
        let plans = RunPlanner.plan(scheme: scheme(), destinations: [DestinationKey(runtime: ios18, model: iphone15), DestinationKey(runtime: ios175, model: iphone15)])
        guard case .matrix(let request)? = plans.first else { return XCTFail("expected a matrix run") }
        XCTAssertEqual(request.models, [iphone15])
        XCTAssertEqual(request.runtimes?.count, 2)
    }

    func testAnIrregularSelectionIsNotWidenedIntoAGrid() {
        // (18.0, iPhone) and (17.5, iPad) as a matrix would also run iPhone on 17.5 and iPad on 18.0: not what was ticked.
        let plans = RunPlanner.plan(scheme: scheme(), destinations: [DestinationKey(runtime: ios18, model: iphone15), DestinationKey(runtime: ios175, model: ipad)])
        XCTAssertEqual(plans.count, 2)
        XCTAssertTrue(plans.allSatisfy { if case .single = $0 { return true } else { return false } })
    }

    func testDuplicatesAndAnyAreDroppedWhenSpecificDestinationsExist() {
        let key = DestinationKey(runtime: ios18, model: iphone15)
        XCTAssertEqual(RunPlanner.plan(scheme: scheme(), destinations: [key, key]).count, 1)
        XCTAssertEqual(RunPlanner.plan(scheme: scheme(), destinations: [.any, key]), [.single(RunTestRequest(testTarget: "DemoApp", requiredRuntime: ios18, requiredModelId: iphone15))])
    }

    func testDestinationKeysAreStableIDs() {
        XCTAssertEqual(DestinationKey.any.id, "*|*")
        XCTAssertEqual(DestinationKey(runtime: "r", model: "m").id, "r|m")
        XCTAssertTrue(DestinationKey.any.isAny)
    }
}
