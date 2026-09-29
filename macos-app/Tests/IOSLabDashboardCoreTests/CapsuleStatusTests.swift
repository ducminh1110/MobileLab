import XCTest
@testable import IOSLabDashboardCore

/// The status table of the design spec, row by row.
final class CapsuleStatusTests: XCTestCase {
    private let now = date("2026-09-29T09:50:00Z")

    private func state(jobs: [TestJob] = [], devices: [Device] = [], demo: Bool = false, connection: ConnectionState = .connected) -> DashboardState {
        var state = DashboardState()
        state.reduce(.devices(DevicesResponse(items: devices)))
        state.reduce(.jobs(jobs))
        state.reduce(.runs([]))
        state.reduce(.connection(connection))
        if demo { state.reduce(.health(Health(mode: .demo))) }
        return state
    }

    private func status(_ scheme: String?, _ state: DashboardState) -> CapsuleStatus {
        CapsuleStatus.make(scheme: scheme, state: state, now: now, formatting: utcFormatting)
    }

    private func status(_ state: DashboardState) -> CapsuleStatus { status("DemoApp", state) }

    private func device(_ id: String, _ status: DeviceStatus) -> Device { Device(id: id, name: "iPhone \(id)", runtime: "com.apple.CoreSimulator.SimRuntime.iOS-18-0", status: status) }

    func testNoJobYetWithBootedSimulators() {
        let s = status(state(devices: [device("1", .ready), device("2", .busy), device("3", .stopped)]))
        XCTAssertEqual(s.state, "Ready")
        XCTAssertEqual(s.detail, "2 simulators booted")
        XCTAssertEqual(s.text, "Ready | 2 simulators booted")
        XCTAssertEqual(s.tone, .neutral)
        XCTAssertNil(s.jobID)
    }

    func testOneSimulatorIsSingular() {
        XCTAssertEqual(status(state(devices: [device("1", .ready)])).detail, "1 simulator booted")
    }

    func testSimulatorsExistButNoneBooted() {
        XCTAssertEqual(status(state(devices: [device("1", .stopped)])).detail, "0 simulators booted")
    }

    func testNoSimulatorsAtAll() {
        XCTAssertEqual(status(state()).detail, "No simulators")
    }

    func testQueuedShowsTheWaitingReason() {
        let job = makeJob(status: .queued, attempts: 0, waiting: "Creating a simulator for this job")
        let s = status(state(jobs: [job]))
        XCTAssertEqual(s.state, "Queued")
        XCTAssertEqual(s.detail, "Creating a simulator for this job")
        XCTAssertEqual(s.glyph, .queued)
        XCTAssertEqual(s.jobID, "job-1")
        XCTAssertEqual(status(state(jobs: [makeJob(status: .queued, attempts: 0)])).detail, "Waiting to start")
    }

    func testRunningShowsSchemeDeviceAndTickingElapsedTime() {
        let job = makeJob(status: .running, started: "2026-09-29T09:49:53Z", device: "iPhone 15")
        let s = status(state(jobs: [job]))
        XCTAssertEqual(s.state, "Running")
        XCTAssertEqual(s.detail, "DemoApp on iPhone 15, 00:07")
        XCTAssertTrue(s.showsSpinner)
        XCTAssertEqual(s.tone, .running)

        let later = CapsuleStatus.make(scheme: "DemoApp", state: state(jobs: [job]), now: now.addingTimeInterval(65), formatting: utcFormatting)
        XCTAssertEqual(later.detail, "DemoApp on iPhone 15, 01:12", "the clock ticks with `now`")
    }

    func testRunningWithoutADeviceYet() {
        let job = makeJob(status: .running, started: "2026-09-29T09:49:59Z", device: nil)
        XCTAssertEqual(status(state(jobs: [job])).detail, "DemoApp, 00:01")
    }

    func testRetryingShowsTheNextAttempt() throws {
        let retrying = try Fixture.decode(TestJob.self, "job-retrying")
        let s = status("FlakyTests", state(jobs: [retrying]))
        XCTAssertEqual(s.state, "Retrying")
        XCTAssertEqual(s.detail, "attempt 2 of 3")
        XCTAssertFalse(s.showsSpinner)
    }

    func testPassedShowsWhenItFinished() {
        let job = makeJob(status: .completed, started: "2026-09-29T09:41:00Z", finished: "2026-09-29T09:41:20Z", summary: JobTestSummary(total: 8, passed: 8))
        let s = status(state(jobs: [job]))
        XCTAssertEqual(s.state, "Tests Passed")
        XCTAssertEqual(s.detail, "Today at 9:41 AM")
        XCTAssertEqual(s.text, "Tests Passed | Today at 9:41 AM")
        XCTAssertEqual(s.tone, .success)
        XCTAssertEqual(s.glyph, .passed)
    }

    func testPassedYesterdayAndLongAgo() {
        let yesterday = makeJob(status: .completed, finished: "2026-09-28T21:05:00Z", summary: JobTestSummary(total: 3, passed: 3))
        XCTAssertEqual(status(state(jobs: [yesterday])).detail, "Yesterday at 9:05 PM")
        let old = makeJob(status: .completed, finished: "2026-08-03T00:30:00Z", summary: JobTestSummary(total: 3, passed: 3))
        XCTAssertEqual(status(state(jobs: [old])).detail, "Aug 3 at 12:30 AM")
    }

    func testCompletedWithZeroTestsIsNotClaimedAsPassed() {
        let job = makeJob(status: .completed, finished: "2026-09-29T09:41:00Z", summary: JobTestSummary(total: 0))
        let s = status(state(jobs: [job]))
        XCTAssertEqual(s.state, "Completed")
        XCTAssertEqual(s.detail, "No tests were run")
        XCTAssertEqual(s.tone, .warning)
    }

    func testTestsFailedCountsThem() throws {
        let job = try Fixture.job("LoginFailTests")
        let s = status("LoginFailTests", state(jobs: [job]))
        XCTAssertEqual(s.state, "Tests Failed")
        XCTAssertEqual(s.detail, "2 of 8 tests failed")
        XCTAssertEqual(s.tone, .failure)
        XCTAssertEqual(s.glyph, .failed)
    }

    func testBuildFailedShowsTheFirstError() throws {
        let job = try Fixture.job("MissingScheme")
        let s = status("MissingScheme", state(jobs: [job]))
        XCTAssertEqual(s.state, "Build Failed")
        XCTAssertEqual(s.detail, job.summary?.errors.first)
        XCTAssertTrue(s.detail?.contains("does not contain a scheme") ?? false)
    }

    func testFailedWithoutSummaryShowsTheJobError() {
        let job = makeJob(status: .failed, finished: "2026-09-29T09:41:00Z", error: "Interrupted: the backend restarted while this job was running. Re-run it to try again.")
        let s = status(state(jobs: [job]))
        XCTAssertEqual(s.state, "Failed")
        XCTAssertTrue(s.detail?.hasPrefix("Interrupted:") ?? false)
        let silent = makeJob(status: .failed)
        XCTAssertEqual(status(state(jobs: [silent])).detail, "The job did not finish")
    }

    func testCancelledShowsTime() throws {
        let job = try Fixture.job("SlowSuite")
        let s = CapsuleStatus.make(scheme: "SlowSuite", state: state(jobs: [job]), now: job.finishedAt!.addingTimeInterval(30), formatting: utcFormatting)
        XCTAssertEqual(s.state, "Cancelled")
        XCTAssertTrue(s.detail?.hasPrefix("Today at ") ?? false)
        XCTAssertEqual(s.glyph, .cancelled)
    }

    func testDisconnectedOverridesEverythingButKeepsTheDemoTag() {
        let job = makeJob(status: .running, started: "2026-09-29T09:49:53Z")
        let s = status(state(jobs: [job], demo: true, connection: .disconnected(reason: "refused")))
        XCTAssertEqual(s.state, "Disconnected")
        XCTAssertEqual(s.detail, "Retrying\u{2026}")
        XCTAssertEqual(s.tone, .disconnected)
        XCTAssertTrue(s.isDemo)
    }

    func testConnectingBeforeAnythingLoaded() {
        var initial = DashboardState()
        XCTAssertEqual(CapsuleStatus.make(scheme: nil, state: initial, now: now, formatting: utcFormatting).state, "Connecting")
        initial.reduce(.devices(DevicesResponse(items: [])))
        initial.reduce(.jobs([]))
        initial.reduce(.runs([]))
        XCTAssertEqual(CapsuleStatus.make(scheme: nil, state: initial, now: now, formatting: utcFormatting).state, "Ready")
    }

    func testDemoTag() {
        XCTAssertTrue(status(state(demo: true)).isDemo)
        XCTAssertFalse(status(state(demo: false)).isDemo)
    }

    // MARK: Which job

    func testDescribesTheSelectedSchemesLatestJob() {
        let older = makeJob("a", scheme: "A", status: .completed, created: "2026-09-29T08:00:00Z", finished: "2026-09-29T08:01:00Z")
        let newer = makeJob("b", scheme: "A", status: .failed, created: "2026-09-29T09:00:00Z", finished: "2026-09-29T09:01:00Z", summary: JobTestSummary(total: 4, passed: 3, failed: 1))
        let other = makeJob("c", scheme: "B", status: .completed, created: "2026-09-29T09:30:00Z", finished: "2026-09-29T09:31:00Z")
        let s = status("A", state(jobs: [older, newer, other]))
        XCTAssertEqual(s.jobID, "b")
        XCTAssertEqual(s.state, "Tests Failed")
    }

    func testARunningJobBeatsNewerFinishedOnes() {
        let running = makeJob("run", scheme: "A", status: .running, created: "2026-09-29T08:00:00Z", started: "2026-09-29T09:49:00Z")
        let done = makeJob("done", scheme: "A", status: .completed, created: "2026-09-29T09:00:00Z", finished: "2026-09-29T09:01:00Z")
        XCTAssertEqual(status("A", state(jobs: [running, done])).jobID, "run")
    }

    func testARunningJobOfAnotherSchemeIsStillShown() {
        let running = makeJob("run", scheme: "B", status: .running, started: "2026-09-29T09:49:00Z")
        let s = status("A", state(jobs: [running]))
        XCTAssertEqual(s.state, "Running")
        XCTAssertTrue(s.detail?.hasPrefix("B on ") ?? false, "the detail names the scheme, so nothing is misleading")
    }

    func testASchemeThatNeverRanIsReadyEvenIfOthersDid() {
        let other = makeJob("c", scheme: "B", status: .completed, finished: "2026-09-29T09:31:00Z")
        XCTAssertEqual(status("A", state(jobs: [other], devices: [device("1", .ready)])).state, "Ready")
    }

    func testNoSchemeSelectedDescribesTheLatestOverall() {
        let job = makeJob(scheme: "Anything", status: .completed, finished: "2026-09-29T09:41:00Z", summary: JobTestSummary(total: 1, passed: 1))
        XCTAssertEqual(status(nil, state(jobs: [job])).state, "Tests Passed")
    }
}
