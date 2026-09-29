import XCTest
@testable import IOSLabDashboardCore

final class PresentationTests: XCTestCase {
    private let now = date("2026-09-29T16:30:00Z")

    private func loadedState() throws -> DashboardState {
        var state = DashboardState()
        state.reduce(.health(try Fixture.decode(Health.self, "health")))
        state.reduce(.capabilities(try Fixture.decode(BackendCapabilities.self, "capabilities")))
        state.reduce(.catalog(try Fixture.decode(Catalog.self, "catalog")))
        state.reduce(.devices(try Fixture.decode(DevicesResponse.self, "devices-ephemeral")))
        state.reduce(.jobs(try Fixture.decode(JobsResponse.self, "tests").items))
        state.reduce(.runs(try Fixture.decode(RunsResponse.self, "runs").items))
        let failing = try Fixture.job("LoginFailTests")
        state.reduce(.results(jobID: failing.id, try Fixture.decode(JobResults.self, "results-fail")))
        return state
    }

    // MARK: Variables view

    func testVariablesViewGroupsCasesWithBadgesAndValues() throws {
        let failing = try Fixture.job("LoginFailTests")
        let results = try Fixture.decode(JobResults.self, "results-fail")
        let rows = VariablesTree.build(job: failing, results: results, runningTest: nil, mode: .all)
        XCTAssertEqual(rows.map(\.title), ["LoginTests", "CheckoutTests", "SettingsTests"])
        XCTAssertEqual(rows[0].value, "= 1 of 3 failed")
        XCTAssertEqual(rows[2].value, "= 2 tests")
        let cases = rows[0].children
        XCTAssertEqual(cases.map(\.badge), [NodeBadge("P", .pass), NodeBadge("F", .fail), NodeBadge("P", .pass)])
        XCTAssertEqual(cases[0].value, "= passed: 0.020 s")
        XCTAssertEqual(cases[1].value, "= failed: 0.030 s")
        XCTAssertEqual(cases[1].children.first?.title, "message")
        XCTAssertTrue(cases[1].children.first?.value?.contains("XCTAssertEqual failed") ?? false)
        XCTAssertTrue(cases[0].children.isEmpty)
    }

    func testVariablesFailuresModeAndFilter() throws {
        let failing = try Fixture.job("LoginFailTests")
        let results = try Fixture.decode(JobResults.self, "results-fail")
        let failures = VariablesTree.build(job: failing, results: results, runningTest: nil, mode: .failures)
        XCTAssertEqual(failures.flatMap(\.children).map(\.title), ["testInvalidPassword", "testPaymentDeclined"])
        let filtered = VariablesTree.build(job: failing, results: results, runningTest: nil, mode: .all, query: "coupon")
        XCTAssertEqual(filtered.flatMap(\.children).map(\.title), ["testApplyCoupon"])
    }

    func testVariablesRunningModeShowsTheRunningTest() throws {
        let running = try Fixture.decode(TestJob.self, "job-running")
        XCTAssertEqual(VariablesTree.build(job: running, results: nil, runningTest: "testInvalidPassword", mode: .running).map(\.title), ["testInvalidPassword"])
        XCTAssertTrue(VariablesTree.build(job: running, results: nil, runningTest: nil, mode: .running).isEmpty)
    }

    func testVariablesWithoutResultsShowJobFieldsOnly() throws {
        let running = try Fixture.decode(TestJob.self, "job-running")
        let rows = VariablesTree.build(job: running, results: nil, runningTest: nil, mode: .all)
        XCTAssertEqual(rows.map(\.title), ["scheme", "status", "attempt", "device"])
        XCTAssertEqual(rows[1].value, "= \"running\"")
        XCTAssertTrue(VariablesTree.build(job: nil, results: nil, runningTest: nil, mode: .all).isEmpty)
    }

    // MARK: Find

    func testFindDevices() throws {
        let state = try loadedState()
        let groups = FindEngine.search(query: "17.5", scope: .devices, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertEqual(groups.count, 1)
        XCTAssertEqual(groups[0].matches.count, 1)
        let match = groups[0].matches[0]
        XCTAssertEqual(match.text, "iPhone 15 (iOS 17.5) (iOS 17.5)")
        XCTAssertNotNil(match.target?.deviceID)
        let range = match.range
        XCTAssertEqual(String(match.text.dropFirst(range.lowerBound).prefix(range.count)), "17.5")
        XCTAssertTrue(FindEngine.search(query: "   ", scope: .devices, state: state, log: nil, openJobID: nil, formatting: utcFormatting).isEmpty)
    }

    func testFindTestsInJobsAndLoadedResults() throws {
        let state = try loadedState()
        let byName = FindEngine.search(query: "paymentdeclined", scope: .tests, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertEqual(byName.count, 1)
        XCTAssertEqual(byName[0].matches.map(\.text), ["CheckoutTests.testPaymentDeclined"])
        XCTAssertEqual(byName[0].matches[0].tab, .tests)

        let byMessage = FindEngine.search(query: "not equal", scope: .tests, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertEqual(byMessage.flatMap(\.matches).count, 2)
        XCTAssertEqual(byMessage.first?.matches.first?.tab, .logs)
        XCTAssertEqual(byMessage.first?.matches.first?.focus, "LoginTests.swift:42")

        let byJob = FindEngine.search(query: "MissingScheme", scope: .tests, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertEqual(byJob.flatMap(\.matches).count, 1)
    }

    func testFindInEventsAndLog() throws {
        var state = try loadedState()
        for event in try Fixture.streamedEvents().filter({ !$0.isOutput }) { state.reduce(.event(event)) }
        let events = FindEngine.search(query: "queued loginfail", scope: .events, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertTrue(events.isEmpty, "the query is one literal string")
        let hits = FindEngine.search(query: "Queued LoginFailTests", scope: .events, state: state, log: nil, openJobID: nil, formatting: utcFormatting)
        XCTAssertEqual(hits.first?.matches.count, 1)
        XCTAssertTrue(hits.first?.matches.first?.text.hasSuffix("Queued LoginFailTests") ?? false)

        let failing = try Fixture.job("LoginFailTests")
        let document = LogDocument(text: try Fixture.decode(JobOutput.self, "output-fail").text)
        let log = FindEngine.search(query: "testInvalidPassword", scope: .log, state: state, log: document, openJobID: failing.id, formatting: utcFormatting)
        XCTAssertEqual(log.first?.matches.count, 3)
        XCTAssertEqual(log.first?.matches.map(\.line), document.lines.filter { $0.text.contains("testInvalidPassword") }.map { $0.number })
        XCTAssertTrue(FindEngine.search(query: "x", scope: .log, state: state, log: nil, openJobID: nil, formatting: utcFormatting).isEmpty)
    }

    // MARK: Breadcrumbs

    func testBreadcrumbsForADevice() throws {
        let state = try loadedState()
        let device = try XCTUnwrap(state.devices.first { !$0.ephemeral })
        let crumbs = Breadcrumbs.build(target: .device(device.id), tab: .summary, state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(crumbs.map(\.title), ["MobileLab", "Simulators", "iOS 18.0", "iPhone 15 Test"])
        XCTAssertEqual(crumbs.last?.target, .device(device.id))
        XCTAssertEqual(crumbs.first?.target, .welcome)
    }

    func testBreadcrumbsForAReportListSiblings() throws {
        let state = try loadedState()
        let job = try Fixture.job("DemoApp", status: .completed)
        let crumbs = Breadcrumbs.build(target: .job(job.id), tab: .tests, state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(crumbs.map(\.title), ["MobileLab", "Reports", "DemoApp", "Tests"])
        let scheme = crumbs[2]
        XCTAssertFalse(scheme.siblings.isEmpty, "other jobs of the scheme, to switch between reports")
        XCTAssertTrue(scheme.siblings.allSatisfy { $0.target?.jobID != nil && $0.target?.jobID != job.id })
        XCTAssertTrue(scheme.siblings[0].title.hasPrefix("DemoApp, Today at "))
    }

    func testBreadcrumbsForRunsWelcomeAndUnknowns() throws {
        let state = try loadedState()
        let run = try XCTUnwrap(state.runs.first { $0.name == "Matrix" })
        XCTAssertEqual(Breadcrumbs.build(target: .run(run.id), tab: .summary, state: state, now: now, formatting: utcFormatting).map(\.title), ["MobileLab", "Runs", "Matrix"])
        XCTAssertEqual(Breadcrumbs.build(target: .welcome, tab: .summary, state: state, now: now, formatting: utcFormatting).map(\.title), ["MobileLab"])
        XCTAssertEqual(Breadcrumbs.build(target: .job("gone"), tab: .logs, state: state, now: now, formatting: utcFormatting).last?.title, "Unknown job")
        XCTAssertEqual(Breadcrumbs.build(target: .device("gone"), tab: .logs, state: state, now: now, formatting: utcFormatting).last?.title, "Unknown device")
    }

    // MARK: Inspector

    func testInspectorForADevice() throws {
        let state = try loadedState()
        let busy = try XCTUnwrap(state.devices.first { $0.status == .busy })
        let sections = Inspector.sections(for: .device(busy.id), state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(sections.map(\.title), ["Identity and Type", "Location", "Resources", "Status"])
        let identity = sections[0].rows
        XCTAssertEqual(identity.map(\.label), ["Name", "Runtime", "Device type", "Backend", "Created"])
        XCTAssertEqual(identity.first { $0.label == "Backend" }?.value, "simctl")
        let udid = try XCTUnwrap(sections[1].rows.first)
        XCTAssertTrue(udid.copyable && udid.monospaced)
        XCTAssertEqual(udid.value, busy.simulatorUdid)
        let job = try XCTUnwrap(sections[2].rows.first { $0.label == "Current job" })
        XCTAssertEqual(job.link, .job(try XCTUnwrap(busy.currentJobId)))
        XCTAssertEqual(sections[3].rows.first?.value, "Busy")
    }

    func testInspectorForAFailedJob() throws {
        let state = try loadedState()
        let failing = try Fixture.job("LoginFailTests")
        let sections = Inspector.sections(for: .job(failing.id), state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(sections.map(\.title), ["Identity", "Destination", "Execution", "Result", "Error"])
        XCTAssertEqual(sections[0].rows.first?.value, failing.id)
        let execution = Dictionary(uniqueKeysWithValues: sections[2].rows.map { ($0.label, $0.value) })
        XCTAssertEqual(execution["Status"], "Failed")
        XCTAssertEqual(execution["Attempts"], "1 of 1")
        XCTAssertEqual(execution["Exit code"], "65")
        XCTAssertNotNil(execution["Duration"])
        XCTAssertNotNil(execution["Started"])
        let result = Dictionary(uniqueKeysWithValues: sections[3].rows.map { ($0.label, $0.value) })
        XCTAssertEqual(result["Total"], "8")
        XCTAssertEqual(result["Failed"], "2")
        XCTAssertEqual(sections[4].rows.first?.value, "2 of 8 tests failed")
    }

    func testInspectorForAJobInAMatrixRunLinksToTheRun() throws {
        let state = try loadedState()
        let inRun = try XCTUnwrap(state.jobs.first { $0.runId != nil })
        let row = Inspector.sections(for: .job(inRun.id), state: state, now: now, formatting: utcFormatting)[0].rows.first { $0.label == "Run" }
        XCTAssertEqual(row?.link, .run(inRun.runId!))
        XCTAssertEqual(row?.value, "Matrix")
    }

    func testInspectorForRunAndBackend() throws {
        let state = try loadedState()
        let run = try XCTUnwrap(state.runs.first { $0.name == "Matrix" })
        let sections = Inspector.sections(for: .run(run.id), state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(sections.map(\.title), ["Identity", "Progress"])
        XCTAssertEqual(sections[1].rows.first { $0.label == "Jobs" }?.value, "4")
        XCTAssertEqual(sections[1].rows.first { $0.label == "Max parallel" }?.value, "2")

        let backend = Inspector.sections(for: .welcome, state: state, now: now, formatting: utcFormatting)
        XCTAssertEqual(backend.map(\.title), ["Backend"])
        let rows = Dictionary(uniqueKeysWithValues: backend[0].rows.map { ($0.label, $0.value) })
        XCTAssertEqual(rows["Version"], "0.1.0")
        XCTAssertEqual(rows["Mode"], "Demo (simulated)")
        XCTAssertEqual(rows["API token"], "Not required")
        XCTAssertEqual(rows["CPU cores"], "4")
        XCTAssertEqual(rows["Capacity"], "\(state.capacity!.load) of 4 units")
        XCTAssertTrue(Inspector.sections(for: .welcome, state: DashboardState(), now: now, formatting: utcFormatting).isEmpty, "nothing is invented before the backend has answered")
    }

    func testInspectorForVirtualMachine() throws {
        var state = DashboardState()
        state.reduce(.devices(try Fixture.decode(DevicesResponse.self, "devices-vm")))
        let vm = try XCTUnwrap(state.devices.first)
        let resources = Inspector.sections(for: .device(vm.id), state: state, now: now, formatting: utcFormatting).first { $0.title == "Resources" }?.rows ?? []
        XCTAssertEqual(resources.first { $0.label == "Cost units" }?.value, "4")
        XCTAssertEqual(resources.first { $0.label == "Screen" }?.value, "1170x2532")
        XCTAssertEqual(resources.first { $0.label == "Memory" }?.value, "4 GB")
    }

    func testHistoryIsNewestFirstAndSkipsOutput() throws {
        let events = try Fixture.streamedEvents()
        let rows = Inspector.history(events: events, formatting: utcFormatting)
        XCTAssertEqual(rows.count, events.filter { !$0.isOutput }.count)
        XCTAssertEqual(rows.first?.text, events.filter { !$0.isOutput }.max { $0.id < $1.id }?.message)
        XCTAssertEqual(rows.first?.time.count, 8)
        XCTAssertTrue(rows.contains { $0.isError })
    }

    func testQuickHelpDependsOnStatus() throws {
        var state = try loadedState()
        let failing = try Fixture.job("LoginFailTests")
        XCTAssertTrue(Inspector.quickHelp(for: .job(failing.id), state: state).contains("At least one test failed"))
        let missing = try Fixture.job("MissingScheme")
        XCTAssertTrue(Inspector.quickHelp(for: .job(missing.id), state: state).contains("build failed"))
        XCTAssertTrue(Inspector.quickHelp(for: .job("gone"), state: state).contains("no longer known"))
        let busy = try XCTUnwrap(state.devices.first { $0.status == .busy })
        XCTAssertTrue(Inspector.quickHelp(for: .device(busy.id), state: state).contains("job is running"))
        XCTAssertTrue(Inspector.quickHelp(for: .welcome, state: state).contains("Press Run"))
        state.reduce(.devices(DevicesResponse(items: [])))
        XCTAssertTrue(Inspector.quickHelp(for: .welcome, state: state).contains("no simulators yet"))
    }

    // MARK: Open Quickly

    func testFuzzyMatching() {
        XCTAssertNil(FuzzyMatcher.score(query: "xyz", in: "LoginTests"))
        XCTAssertNotNil(FuzzyMatcher.score(query: "lgt", in: "LoginTests"))
        XCTAssertEqual(FuzzyMatcher.score(query: "", in: "anything"), 0)
        let prefix = try! XCTUnwrap(FuzzyMatcher.score(query: "log", in: "LoginTests"))
        let scattered = try! XCTUnwrap(FuzzyMatcher.score(query: "log", in: "Something Long Ago"))
        XCTAssertGreaterThan(prefix, scattered)
        let camel = try! XCTUnwrap(FuzzyMatcher.score(query: "lt", in: "LoginTests"))
        let plain = try! XCTUnwrap(FuzzyMatcher.score(query: "lt", in: "alterations"))
        XCTAssertGreaterThan(camel, plain, "capitals and word starts count")
        XCTAssertNotNil(FuzzyMatcher.score(query: "ip15", in: "iPhone 15 Test"))
        XCTAssertNil(FuzzyMatcher.score(query: "tset", in: "test"), "order matters")
    }

    func testOpenQuicklyIndexesEverythingReal() throws {
        let state = try loadedState()
        let items = QuickOpen.index(state: state, formatting: utcFormatting, now: now)
        XCTAssertEqual(items.filter { $0.kind == .device }.count, state.devices.count)
        XCTAssertEqual(items.filter { $0.kind == .run }.count, state.runs.count)
        XCTAssertEqual(items.filter { $0.kind == .job }.count, state.jobs.count)
        XCTAssertEqual(items.filter { $0.kind == .test }.count, 8, "test cases of jobs whose results are loaded")
        XCTAssertEqual(Set(items.map(\.id)).count, items.count, "ids are unique")
    }

    func testOpenQuicklySearch() throws {
        let state = try loadedState()
        let items = QuickOpen.index(state: state, formatting: utcFormatting, now: now)
        let hits = QuickOpen.search("paydecl", in: items)
        XCTAssertEqual(hits.first?.title, "CheckoutTests.testPaymentDeclined")
        XCTAssertEqual(hits.first?.kind, .test)
        XCTAssertEqual(hits.first?.tab, .logs)
        XCTAssertEqual(hits.first?.focus, "CheckoutTests.swift:46")
        XCTAssertEqual(QuickOpen.search("matrix", in: items).first?.kind, .run)
        XCTAssertEqual(QuickOpen.search("", in: items, limit: 5).count, 5)
        XCTAssertTrue(QuickOpen.search("qqqqqq", in: items).isEmpty)
        let devices = QuickOpen.search("iphone 15 test", in: items).filter { $0.kind == .device }
        XCTAssertEqual(devices.first?.title, "iPhone 15 Test")
    }
}
