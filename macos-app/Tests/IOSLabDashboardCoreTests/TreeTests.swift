import XCTest
@testable import IOSLabDashboardCore

final class TreeTests: XCTestCase {
    private let now = date("2026-09-29T16:30:00Z")

    private func ids(_ rows: [FlatRow]) -> [String] { rows.map(\.id) }

    // MARK: Devices

    func testDevicesTreeGroupsByRuntimeNewestFirst() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-ephemeral").items
        let extra = [
            Device(id: "x1", name: "iPad Pro", runtime: "com.apple.CoreSimulator.SimRuntime.iOS-18-2", runtimeName: "iOS 18.2", modelName: "iPad Pro 13-inch", status: .stopped),
            Device(id: "x2", name: "iPhone 16", runtime: "com.apple.CoreSimulator.SimRuntime.iOS-18-2", runtimeName: "iOS 18.2", modelName: "iPhone 16", status: .error)
        ]
        let roots = DevicesTree.build(devices: devices + extra, vmEnabled: false)

        XCTAssertEqual(roots.map(\.title), ["Simulators"])
        let folders = roots[0].children
        XCTAssertEqual(folders.map(\.title), ["iOS 18.2", "iOS 18.0", "iOS 17.5"], "newest runtime first")
        XCTAssertTrue(folders.allSatisfy { $0.icon == .folder })
        XCTAssertEqual(folders[0].children.map(\.title), ["iPad Pro", "iPhone 16"])
        XCTAssertEqual(folders[0].children[0].icon, .ipad)
        XCTAssertEqual(folders[0].children[0].dot, .stopped)
        XCTAssertEqual(folders[0].children[1].dot, .error)
    }

    func testAutoTagAndTargets() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-ephemeral").items
        let roots = DevicesTree.build(devices: devices, vmEnabled: false)
        let auto = try XCTUnwrap(TreeFlattener.find(DevicesTree.deviceNodeID(devices.first { $0.ephemeral }!.id), in: roots))
        XCTAssertEqual(auto.tags, ["auto"])
        XCTAssertEqual(auto.dot, .busy)
        XCTAssertEqual(auto.target, .device(devices.first { $0.ephemeral }!.id))
        let manual = try XCTUnwrap(TreeFlattener.find(DevicesTree.deviceNodeID(devices.first { !$0.ephemeral }!.id), in: roots))
        XCTAssertTrue(manual.tags.isEmpty)
        XCTAssertEqual(manual.dot, .ready)
    }

    func testVMRootAppearsOnlyWhenEnabledOrPresent() throws {
        let vm = try Fixture.decode(DevicesResponse.self, "devices-vm").items
        XCTAssertEqual(DevicesTree.build(devices: [], vmEnabled: false).map(\.id), [DevicesTree.simulatorsRootID])
        XCTAssertEqual(DevicesTree.build(devices: [], vmEnabled: true).map(\.title), ["Simulators", "Experimental VMs (simulated)"])
        let roots = DevicesTree.build(devices: vm, vmEnabled: false)
        XCTAssertEqual(roots.map(\.title), ["Simulators", "Experimental VMs (simulated)"], "a VM that exists is always shown")
        XCTAssertEqual(roots[1].children.first?.icon, .vm)
        XCTAssertTrue(roots[0].children.isEmpty)
    }

    func testBootedOnlyToggle() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-after").items
        let all = DevicesTree.build(devices: devices, vmEnabled: false)
        let booted = DevicesTree.build(devices: devices, vmEnabled: false, filter: NavigatorFilter(onlyProblems: true))
        XCTAssertEqual(TreeFlattener.flatten(all, expanded: TreeFlattener.expandableIDs(all)).filter { $0.node.dot != nil }.count, 2)
        XCTAssertEqual(TreeFlattener.flatten(booted, expanded: TreeFlattener.expandableIDs(booted)).filter { $0.node.dot != nil }.count, 1)
    }

    func testTextFilterKeepsAncestorsOfMatches() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-after").items
        let roots = DevicesTree.build(devices: devices, vmEnabled: false, filter: NavigatorFilter(text: "test"))
        let rows = TreeFlattener.flatten(roots, expanded: [], expandAll: true)
        XCTAssertEqual(rows.map(\.node.title), ["Simulators", "iOS 18.0", "iPhone 15 Test"])
        XCTAssertEqual(rows.map(\.depth), [0, 1, 2])
        XCTAssertTrue(DevicesTree.build(devices: devices, vmEnabled: false, filter: NavigatorFilter(text: "zzz")).isEmpty)
    }

    // MARK: Flattening

    func testFlattenRespectsExpansion() {
        let tree = [
            TreeNode(id: "a", title: "A", children: [TreeNode(id: "a1", title: "A1", children: [TreeNode(id: "a1x", title: "A1x")]), TreeNode(id: "a2", title: "A2")]),
            TreeNode(id: "b", title: "B", lazyJobID: "j")
        ]
        XCTAssertEqual(ids(TreeFlattener.flatten(tree, expanded: [])), ["a", "b"])
        XCTAssertEqual(ids(TreeFlattener.flatten(tree, expanded: ["a"])), ["a", "a1", "a2", "b"])
        XCTAssertEqual(ids(TreeFlattener.flatten(tree, expanded: ["a", "a1"])), ["a", "a1", "a1x", "a2", "b"])
        XCTAssertEqual(ids(TreeFlattener.flatten(tree, expanded: [], expandAll: true)), ["a", "a1", "a1x", "a2", "b"])
        let rows = TreeFlattener.flatten(tree, expanded: ["a"])
        XCTAssertEqual(rows.map(\.depth), [0, 1, 1, 0])
        XCTAssertEqual(rows.map(\.isExpanded), [true, false, false, false])
        XCTAssertEqual(rows.map(\.isExpandable), [true, true, false, true], "a lazy node is expandable before its children exist")
        XCTAssertEqual(TreeFlattener.expandableIDs(tree), ["a", "a1", "b"])
    }

    func testHeadersAreAlwaysOpen() {
        let tree = [TreeNode(id: "h", title: "Today", children: [TreeNode(id: "c", title: "child")], isHeader: true)]
        XCTAssertEqual(ids(TreeFlattener.flatten(tree, expanded: [])), ["h", "c"])
    }

    // MARK: Tests navigator

    func testTestsTreeShowsRunsJobsAndStandaloneJobs() throws {
        let jobs = try Fixture.decode(JobsResponse.self, "tests").items
        let runs = try Fixture.decode(RunsResponse.self, "runs").items
        let roots = TestsTree.build(runs: runs, jobs: jobs, results: [:])

        XCTAssertEqual(roots.count, 1)
        XCTAssertEqual(roots[0].title, "Runs")
        let children = roots[0].children
        let matrix = try XCTUnwrap(children.first { $0.title == "Matrix" })
        XCTAssertEqual(matrix.children.count, 4)
        XCTAssertEqual(matrix.icon, .diamond(.passed))
        XCTAssertEqual(matrix.trailing, "4 passed")
        XCTAssertEqual(matrix.subtitle, "DemoApp")
        XCTAssertNotNil(matrix.target?.runID)

        let standalone = children.filter { $0.target?.jobID != nil }
        let standaloneScheme = standalone.map { $0.title.split(separator: " ").first.map(String.init) ?? "" }
        XCTAssertTrue(standaloneScheme.contains("LoginFailTests"))
        XCTAssertTrue(standaloneScheme.contains("MissingScheme"))
        XCTAssertEqual(standalone.first { $0.title.hasPrefix("LoginFailTests") }?.icon, .diamond(.failed))
        XCTAssertEqual(standalone.first { $0.title.hasPrefix("SlowSuite") }?.icon, .diamond(.cancelled))
        XCTAssertEqual(standalone.first { $0.title.hasPrefix("LoginFailTests") }?.title, "LoginFailTests on iPhone 15 Test")

        // Every job appears exactly once, either in a run or directly under Runs.
        let jobNodeCount = TreeFlattener.flatten(roots, expanded: TreeFlattener.expandableIDs(roots)).filter { $0.node.id.hasPrefix("job:") }.count
        XCTAssertEqual(jobNodeCount, jobs.count)
    }

    func testJobsAreExpandableBeforeTheirResultsLoad() throws {
        let failing = try Fixture.job("LoginFailTests")
        let roots = TestsTree.build(runs: [], jobs: [failing], results: [:])
        let node = try XCTUnwrap(roots[0].children.first)
        XCTAssertEqual(node.lazyJobID, failing.id, "the view fetches results when the row is first opened")
        XCTAssertTrue(node.children.isEmpty)
        XCTAssertTrue(node.isExpandable)
        let queued = TestsTree.build(runs: [], jobs: [makeJob(status: .queued, attempts: 0)], results: [:])
        XCTAssertNil(queued[0].children.first?.lazyJobID, "a job that never ran has no results to fetch")
        XCTAssertEqual(queued[0].children.first?.trailing, "queued")
    }

    func testResultsBecomeSuitesAndCases() throws {
        let failing = try Fixture.job("LoginFailTests")
        let results = try Fixture.decode(JobResults.self, "results-fail")
        let roots = TestsTree.build(runs: [], jobs: [failing], results: [failing.id: results])
        let job = try XCTUnwrap(roots[0].children.first)
        XCTAssertNil(job.lazyJobID)
        XCTAssertEqual(job.children.map(\.title), ["LoginTests", "CheckoutTests", "SettingsTests"], "suites in first-seen order, module prefix dropped")
        XCTAssertEqual(job.children.map(\.icon), [.diamond(.failed), .diamond(.failed), .diamond(.passed)])
        let login = job.children[0]
        XCTAssertEqual(login.children.map(\.title), ["testValidCredentials", "testInvalidPassword", "testEmptyUsername"])
        XCTAssertEqual(login.children.map(\.icon), [.diamond(.passed), .diamond(.failed), .diamond(.passed)])
        XCTAssertEqual(login.children[1].tab, .logs, "a failed test opens the log at its failure")
        XCTAssertEqual(login.children[1].focus, "LoginTests.swift:42")
        XCTAssertEqual(login.children[0].tab, .tests)
        XCTAssertEqual(login.children[1].trailing, "0.030 s")
    }

    func testFailedOnlyToggle() throws {
        let failing = try Fixture.job("LoginFailTests")
        let passing = try Fixture.job("DemoApp", status: .completed)
        let results = try Fixture.decode(JobResults.self, "results-fail")
        let roots = TestsTree.build(runs: [], jobs: [failing, passing], results: [failing.id: results], filter: NavigatorFilter(onlyProblems: true))
        let jobs = roots[0].children
        XCTAssertEqual(jobs.count, 1)
        let cases = jobs[0].children.flatMap(\.children)
        XCTAssertEqual(cases.map(\.title), ["testInvalidPassword", "testPaymentDeclined"])
    }

    func testRunCountsWording() {
        var run = TestRunView(id: "r", scheme: "S", jobIds: ["a", "b", "c", "d"], createdAt: date("2026-09-29T09:00:00Z"), status: .running, counts: JobCounts(queued: 3, running: 1))
        XCTAssertEqual(JobText.runCounts(run), "1 running, 3 queued")
        run.counts = JobCounts(completed: 3, failed: 1)
        XCTAssertEqual(JobText.runCounts(run), "3 passed, 1 failed")
        run.counts = JobCounts()
        XCTAssertEqual(JobText.runCounts(run), "4 jobs")
    }

    func testJobTitleUsesRequestedDestinationWhenNoDeviceYet() {
        var job = makeJob(status: .queued, attempts: 0, device: nil)
        job.requiredRuntime = "com.apple.CoreSimulator.SimRuntime.iOS-17-5"
        job.requiredModelId = "com.apple.CoreSimulator.SimDeviceType.iPhone-SE-3rd-generation"
        XCTAssertEqual(JobText.title(job), "DemoApp on iPhone SE 3rd generation (iOS 17.5)")
        job.assignedDeviceName = "Lab iPhone"
        XCTAssertEqual(JobText.title(job), "DemoApp on Lab iPhone")
        job.assignedDeviceName = nil; job.requiredRuntime = nil; job.requiredModelId = nil
        XCTAssertEqual(JobText.title(job), "DemoApp")
    }

    // MARK: Issues

    func testIssuesListFailuresWithLocations() throws {
        let failing = try Fixture.job("LoginFailTests")
        let results = try Fixture.decode(JobResults.self, "results-fail")
        let roots = IssuesTree.build(jobs: [failing], results: [failing.id: results], now: now, formatting: utcFormatting)
        XCTAssertEqual(roots.count, 1)
        XCTAssertEqual(roots[0].title, "LoginFailTests on iPhone 15 Test")
        XCTAssertEqual(roots[0].children.count, 2)
        let issue = roots[0].children[0]
        XCTAssertEqual(issue.title, "XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")")
        XCTAssertEqual(issue.trailing, "LoginTests.swift:42")
        XCTAssertEqual(issue.subtitle, "LoginTests.testInvalidPassword")
        XCTAssertEqual(issue.icon, .issue(.testFailure))
        XCTAssertEqual(issue.tab, .logs)
        XCTAssertEqual(issue.focus, "LoginTests.swift:42")
        XCTAssertEqual(IssuesTree.count(jobs: [failing], results: [failing.id: results]), 2)
    }

    func testIssuesBeforeResultsLoadFallBackToTheJobsOwnMessage() throws {
        let failing = try Fixture.job("LoginFailTests")
        let roots = IssuesTree.build(jobs: [failing], results: [:], now: now, formatting: utcFormatting)
        XCTAssertEqual(roots[0].children.map(\.title), ["2 of 8 tests failed"])
    }

    func testBuildErrorsBecomeIssues() throws {
        let missing = try Fixture.job("MissingScheme")
        let roots = IssuesTree.build(jobs: [missing], results: [:], now: now, formatting: utcFormatting)
        let issue = try XCTUnwrap(roots.first?.children.first)
        XCTAssertEqual(issue.icon, .issue(.buildError))
        XCTAssertTrue(issue.title.contains("does not contain a scheme named \"MissingScheme\""))
        XCTAssertEqual(issue.tab, .logs)
    }

    func testCompilerErrorsCarryFileAndLine() {
        let job = makeJob(status: .failed, finished: "2026-09-29T09:00:00Z", summary: JobTestSummary(buildFailed: true, errors: ["LoginView.swift:12: cannot find 'foo' in scope"]))
        let issue = IssuesTree.build(jobs: [job], results: [:], now: now, formatting: utcFormatting)[0].children[0]
        XCTAssertEqual(issue.title, "cannot find 'foo' in scope")
        XCTAssertEqual(issue.trailing, "LoginView.swift:12")
        XCTAssertEqual(issue.focus, "LoginView.swift:12")
    }

    func testAnInterruptedJobIsAnIssueToo() {
        let job = makeJob(status: .failed, finished: "2026-09-29T09:00:00Z", error: "Interrupted: the backend restarted while this job was running. Re-run it to try again.")
        let issue = IssuesTree.build(jobs: [job], results: [:], now: now, formatting: utcFormatting)[0].children[0]
        XCTAssertEqual(issue.icon, .issue(.runtimeError))
    }

    func testNoIssuesForPassingOrCancelledJobs() throws {
        let jobs = try Fixture.decode(JobsResponse.self, "tests").items.filter { $0.status != .failed }
        XCTAssertTrue(IssuesTree.build(jobs: jobs, results: [:], now: now, formatting: utcFormatting).isEmpty)
        XCTAssertEqual(IssuesTree.count(jobs: jobs, results: [:]), 0)
    }

    // MARK: Reports

    func testReportsGroupByDayNewestFirst() {
        let today1 = makeJob("a", status: .completed, created: "2026-09-29T09:00:00Z", started: "2026-09-29T09:00:00Z", finished: "2026-09-29T09:00:12Z")
        let today2 = makeJob("b", status: .failed, created: "2026-09-29T10:00:00Z", started: "2026-09-29T10:00:00Z", finished: "2026-09-29T10:01:05Z")
        let yesterday = makeJob("c", status: .cancelled, created: "2026-09-28T09:00:00Z", finished: "2026-09-28T09:00:03Z")
        let older = makeJob("d", status: .completed, created: "2026-09-01T09:00:00Z", finished: "2026-09-01T09:00:03Z")
        let running = makeJob("e", status: .running, started: "2026-09-29T11:00:00Z")
        let roots = ReportsTree.build(jobs: [today1, running, older, yesterday, today2], now: now, formatting: utcFormatting)

        XCTAssertEqual(roots.map(\.title), ["Today", "Yesterday", "Sep 1"])
        XCTAssertTrue(roots.allSatisfy(\.isHeader))
        XCTAssertEqual(roots[0].children.map(\.id), ["report:b", "report:a"], "newest first; running jobs are not reports yet")
        XCTAssertEqual(roots[0].children[0].trailing, "10:01 AM  1 min 05 s")
        XCTAssertEqual(roots[0].children[1].trailing, "9:00 AM  12.0 s")
        XCTAssertEqual(roots[0].children[0].icon, .diamond(.failed))
        XCTAssertEqual(roots[1].children[0].icon, .diamond(.cancelled))
        let failedOnly = ReportsTree.build(jobs: [today1, today2, yesterday], now: now, formatting: utcFormatting, filter: NavigatorFilter(onlyProblems: true))
        XCTAssertEqual(failedOnly.flatMap(\.children).map(\.id), ["report:b"])
    }

    func testReportsFromRealJobs() throws {
        let jobs = try Fixture.decode(JobsResponse.self, "tests").items
        let roots = ReportsTree.build(jobs: jobs, now: now, formatting: utcFormatting)
        XCTAssertEqual(roots.flatMap(\.children).count, jobs.filter { $0.status.isTerminal }.count)
        XCTAssertEqual(roots.map(\.title), ["Today"])
    }

    // MARK: Debug navigator

    func testDebugNavigatorGaugesComeFromMetrics() throws {
        let metrics = try Fixture.decode(MetricsSummary.self, "metrics-busy")
        let jobs = try Fixture.decode(JobsResponse.self, "tests").items
        let model = DebugNavigator.build(metrics: metrics, capacity: metrics.capacity, jobs: jobs)
        XCTAssertEqual(model.title, "MobileLab Backend")
        XCTAssertEqual(model.pid, metrics.process?.pid)
        XCTAssertEqual(model.gauges.map(\.title), ["CPU", "Memory", "Disk (artifacts)", "Capacity"])
        let cpu = model.gauges[0]
        XCTAssertEqual(cpu.value, Formatters.percent(metrics.process!.cpuPercent))
        XCTAssertEqual(try XCTUnwrap(cpu.fraction), metrics.process!.cpuPercent / 400, accuracy: 0.0001)
        let memory = model.gauges[1]
        XCTAssertEqual(memory.value, Formatters.bytes(metrics.process!.rssBytes))
        XCTAssertEqual(try XCTUnwrap(memory.fraction), Double(metrics.process!.rssBytes) / (15.7 * 1024 * 1024 * 1024), accuracy: 0.0001)
        XCTAssertNil(model.gauges[2].fraction, "there is no limit to measure the artifact store against, so no bar is invented")
        XCTAssertEqual(model.gauges[3].value, "1 of 4")
        XCTAssertEqual(model.gauges[3].fraction, 0.25)
        XCTAssertTrue(model.hasMetrics)
    }

    func testDebugNavigatorWithoutMetricsShowsNothingMadeUp() {
        let model = DebugNavigator.build(metrics: nil, capacity: nil, jobs: [])
        XCTAssertTrue(model.gauges.isEmpty)
        XCTAssertNil(model.pid)
        XCTAssertFalse(model.hasMetrics)
        XCTAssertTrue(model.threads.isEmpty)
    }

    func testRunningJobsAreThreadsAndQueuedOnesAreGreyed() {
        let running = makeJob("r1", scheme: "DemoApp", status: .running, created: "2026-09-29T09:00:00Z", started: "2026-09-29T09:00:01Z", attempts: 1, maxRetries: 2)
        let queued = makeJob("q1", scheme: "Other", status: .queued, created: "2026-09-29T09:01:00Z", attempts: 0, device: nil, waiting: "Waiting for a free simulator")
        let threads = DebugNavigator.build(metrics: nil, capacity: nil, jobs: [queued, running]).threads
        XCTAssertEqual(threads.map(\.title), ["Thread 1", "Thread 2"])
        XCTAssertEqual(threads.map(\.subtitle), ["Queue: DemoApp", "Queue: Other"])
        XCTAssertEqual(threads.map(\.isDimmed), [false, true])
        XCTAssertEqual(threads[0].children.map(\.title), ["0 DemoApp on iPhone 15", "1 attempt 1 of 3", "2 job r1"])
        XCTAssertEqual(threads[1].children.map(\.title), ["0 Waiting for a free simulator"])
    }
}
