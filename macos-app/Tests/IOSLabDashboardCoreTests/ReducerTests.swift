import XCTest
@testable import IOSLabDashboardCore

final class ReducerTests: XCTestCase {
    private func loadedState() throws -> DashboardState {
        var state = DashboardState()
        state.reduce(.health(try Fixture.decode(Health.self, "health")))
        state.reduce(.capabilities(try Fixture.decode(BackendCapabilities.self, "capabilities")))
        state.reduce(.devices(try Fixture.decode(DevicesResponse.self, "devices-after")))
        state.reduce(.jobs(try Fixture.decode(JobsResponse.self, "tests").items))
        state.reduce(.runs(try Fixture.decode(RunsResponse.self, "runs").items))
        return state
    }

    func testStartsEmptyAndConnecting() {
        let state = DashboardState()
        XCTAssertEqual(state.connection, .connecting)
        XCTAssertFalse(state.hasLoaded)
        XCTAssertTrue(state.devices.isEmpty)
        XCTAssertFalse(state.hasActiveWork)
    }

    func testLoadingListsMarksThemLoadedAndKeepsNewestFirst() throws {
        var state = DashboardState()
        state.reduce(.jobs(try Fixture.decode(JobsResponse.self, "tests").items.reversed()))
        XCTAssertTrue(state.jobsLoaded)
        XCTAssertEqual(state.jobs.map(\.createdAt), state.jobs.map(\.createdAt).sorted(by: >))
        XCTAssertFalse(state.hasLoaded, "devices and runs have not arrived yet")
        state.reduce(.devices(try Fixture.decode(DevicesResponse.self, "devices")))
        state.reduce(.runs([]))
        XCTAssertTrue(state.hasLoaded)
        XCTAssertEqual(state.capacity?.maxLoad, 4, "capacity comes with the devices")
        XCTAssertEqual(state.runsLoaded, true)
    }

    func testDemoModeComesFromCapabilitiesOrHealth() throws {
        var state = DashboardState()
        XCTAssertFalse(state.isDemo)
        state.reduce(.health(try Fixture.decode(Health.self, "health")))
        XCTAssertTrue(state.isDemo)
        state.reduce(.capabilities(try BackendJSON.decoder().decode(BackendCapabilities.self, from: Data(#"{"platform":"darwin","architecture":"arm64","supportedTargets":["ios-simulator"],"mode":"live"}"#.utf8))))
        XCTAssertFalse(state.isDemo, "capabilities are more specific than health")
    }

    func testBootedSimulatorCountAndActiveWork() throws {
        var state = try loadedState()
        XCTAssertEqual(state.bootedSimulatorCount, 1, "one ready, one shutting down")
        XCTAssertFalse(state.hasActiveWork)
        state.reduce(.upsertJob(makeJob("busy", status: .running, started: "2026-09-29T09:00:00Z")))
        XCTAssertTrue(state.hasActiveWork)
    }

    func testResultsAreKeptOnlyForKnownJobs() throws {
        var state = try loadedState()
        let failing = try Fixture.job("LoginFailTests")
        state.reduce(.results(jobID: failing.id, try Fixture.decode(JobResults.self, "results-fail")))
        state.reduce(.results(jobID: "ghost", JobResults()))
        XCTAssertEqual(state.results.keys.sorted(), [failing.id])

        state.reduce(.jobs(state.jobs.filter { $0.id != failing.id }))
        XCTAssertTrue(state.results.isEmpty, "results of a cleaned-up job are dropped")
    }

    func testUpsertJobReplacesOrInsertsAndAsksForRunRefresh() throws {
        var state = try loadedState()
        let count = state.jobs.count
        var running = makeJob("new-1", status: .running, created: "2026-09-30T10:00:00Z", started: "2026-09-30T10:00:01Z")
        XCTAssertEqual(state.reduce(.upsertJob(running)), [.runs])
        XCTAssertEqual(state.jobs.count, count + 1)
        XCTAssertEqual(state.jobs.first?.id, "new-1")
        running.status = .completed
        state.reduce(.upsertJob(running))
        XCTAssertEqual(state.jobs.count, count + 1)
        XCTAssertEqual(state.job(id: "new-1")?.status, .completed)
    }

    func testDeviceUpsertAndRemoval() throws {
        var state = try loadedState()
        let device = try XCTUnwrap(state.devices.first)
        var changed = device
        changed.status = .stopped
        state.reduce(.upsertDevice(changed))
        XCTAssertEqual(state.device(id: device.id)?.status, .stopped)
        state.reduce(.removeDevice(device.id))
        XCTAssertNil(state.device(id: device.id))
    }

    func testReconnectingRefreshesEverything() {
        var state = DashboardState()
        XCTAssertEqual(state.reduce(.connection(.disconnected(reason: "down"))), [])
        XCTAssertEqual(state.connection, .disconnected(reason: "down"))
        let request = state.reduce(.connection(.connected))
        XCTAssertTrue(request.contains(.devices) && request.contains(.jobs) && request.contains(.runs) && request.contains(.metrics))
        XCTAssertTrue(state.connection.isConnected)
    }

    func testLifecycleEventsAppendToActivityAndAskForRefetch() throws {
        var state = DashboardState()
        let events = try Fixture.streamedEvents().filter { !$0.isOutput }
        var lastRequest: RefreshRequest = []
        for event in events { lastRequest = state.reduce(.event(event)) }
        XCTAssertEqual(state.activity.count, events.count)
        XCTAssertEqual(state.lastEventID, events.last?.id)
        XCTAssertTrue(lastRequest.contains(.devices) && lastRequest.contains(.jobs) && lastRequest.contains(.runs))
    }

    func testOutputEventsNeverTouchTheStateOrTriggerRefetch() throws {
        var state = DashboardState()
        let output = try XCTUnwrap(Fixture.streamedEvents().first { $0.isOutput })
        XCTAssertEqual(state.reduce(.event(output)), [])
        XCTAssertTrue(state.activity.isEmpty)
        XCTAssertEqual(state.lastEventID, 0)
    }

    func testReplayedEventsAreNotAppliedTwice() throws {
        var state = DashboardState()
        let events = try Fixture.streamedEvents().filter { !$0.isOutput }
        for event in events { state.reduce(.event(event)) }
        let count = state.activity.count
        // A reconnect replays history the state already has.
        for event in events { XCTAssertEqual(state.reduce(.event(event)), [], "a duplicate needs no refetch") }
        XCTAssertEqual(state.activity.count, count)
    }

    func testABackendRestartThatReusesEventIDsIsNotMistakenForADuplicate() {
        var state = DashboardState()
        let before = EngineEvent(id: 5, source: "scheduler", type: "log", action: "enqueue_job", message: "Queued A", timestamp: date("2026-09-29T09:00:00Z"))
        let after = EngineEvent(id: 5, source: "scheduler", type: "log", action: "enqueue_job", message: "Queued B", timestamp: date("2026-09-29T10:00:00Z"))
        state.reduce(.event(before))
        state.reduce(.event(after))
        XCTAssertEqual(state.activity.map(\.message), ["Queued A", "Queued B"])
    }

    func testActivityIsBounded() {
        var state = DashboardState()
        for id in 1...(DashboardState.activityLimit + 120) {
            state.reduce(.event(EngineEvent(id: id, source: "system", type: "log", action: "tick", message: "\(id)", timestamp: Date(timeIntervalSince1970: Double(id)))))
        }
        XCTAssertEqual(state.activity.count, DashboardState.activityLimit)
        XCTAssertEqual(state.activity.last?.message, "\(DashboardState.activityLimit + 120)")
        XCTAssertEqual(state.activity.first?.message, "121")
    }

    func testMetricsUpdateCapacity() throws {
        var state = DashboardState()
        state.reduce(.metrics(try Fixture.decode(MetricsSummary.self, "metrics-busy")))
        XCTAssertEqual(state.metrics?.running, 1)
        XCTAssertEqual(state.capacity?.load, 1)
    }

    func testPollingPolicy() {
        XCTAssertEqual(PollingPolicy.listInterval(connection: .disconnected(reason: nil)), 5)
        XCTAssertEqual(PollingPolicy.listInterval(connection: .connecting), 5)
        XCTAssertGreaterThanOrEqual(PollingPolicy.listInterval(connection: .connected), PollingPolicy.minimumInterval)
        XCTAssertEqual(PollingPolicy.metricsInterval(debugNavigatorVisible: true), 2)
        XCTAssertNil(PollingPolicy.metricsInterval(debugNavigatorVisible: false))
        XCTAssertGreaterThanOrEqual(PollingPolicy.metricsInterval(debugNavigatorVisible: true) ?? 0, PollingPolicy.minimumInterval, "never faster than 1s")
    }

    func testDebouncerCollapsesABurstIntoOneCall() async throws {
        let fired = Counter()
        let gate = AsyncGate()
        let debouncer = Debouncer(delay: 0.25, sleep: { _ in await gate.wait() }, action: { _ = fired.next() })
        for _ in 0..<50 { await debouncer.trigger() }
        await gate.open()
        for _ in 0..<200 where fired.current == 0 { try await Task.sleep(nanoseconds: 5_000_000) }
        try await Task.sleep(nanoseconds: 50_000_000)
        XCTAssertEqual(fired.current, 1, "fifty triggers, one refetch")
    }

    func testDebouncerCancelDropsThePendingCall() async throws {
        let fired = Counter()
        let debouncer = Debouncer(delay: 0.05, sleep: { seconds in try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000)) }, action: { _ = fired.next() })
        await debouncer.trigger()
        await debouncer.cancel()
        try await Task.sleep(nanoseconds: 200_000_000)
        XCTAssertEqual(fired.current, 0)
    }
}

/// Holds every waiter until `open()` is called.
actor AsyncGate {
    private var isOpen = false
    private var waiters: [CheckedContinuation<Void, Never>] = []

    func wait() async {
        if isOpen { return }
        await withCheckedContinuation { waiters.append($0) }
    }

    func open() {
        isOpen = true
        for waiter in waiters { waiter.resume() }
        waiters = []
    }
}
