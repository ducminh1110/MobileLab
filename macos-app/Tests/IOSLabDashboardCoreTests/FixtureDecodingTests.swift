import XCTest
@testable import IOSLabDashboardCore

/// Every JSON file in Fixtures/ was captured from a running demo backend. If the backend API changes and a
/// model no longer matches, these are the tests that fail.
final class FixtureDecodingTests: XCTestCase {
    func testHealth() throws {
        let health = try Fixture.decode(Health.self, "health")
        XCTAssertEqual(health.status, "ok")
        XCTAssertEqual(health.mode, .demo)
        XCTAssertEqual(health.version, "0.1.0")
        XCTAssertNotNil(health.timestamp)
        XCTAssertNotNil(health.uptimeSeconds)
    }

    func testCapabilities() throws {
        let caps = try Fixture.decode(BackendCapabilities.self, "capabilities")
        XCTAssertEqual(caps.mode, .demo)
        XCTAssertEqual(caps.modeReason, "env", "capture.sh forces demo mode with IOSLAB_SIMULATOR_MOCK=true")
        XCTAssertEqual(caps.vm?.enabled, true)
        XCTAssertEqual(caps.vm?.canRunTests, false)
        XCTAssertEqual(caps.auth?.required, false)
        XCTAssertEqual(caps.capacity?.maxLoad, 4)
        XCTAssertEqual(caps.capacity?.load, 0)
        XCTAssertNotNil(caps.dataDir)
        XCTAssertNotNil(caps.workspaceRoot)
    }

    func testCatalog() throws {
        let catalog = try Fixture.decode(Catalog.self, "catalog")
        XCTAssertEqual(catalog.source, "demo")
        XCTAssertEqual(catalog.runtimes.map(\.name), ["iOS 18.2", "iOS 18.0", "iOS 17.5"])
        XCTAssertEqual(catalog.deviceTypes.count, 4)
        XCTAssertEqual(catalog.deviceTypes.last?.family, "iPad")

        let ios175 = try XCTUnwrap(catalog.runtime(identifier: "com.apple.CoreSimulator.SimRuntime.iOS-17-5"))
        let pro = try XCTUnwrap(catalog.deviceType(identifier: "com.apple.CoreSimulator.SimDeviceType.iPhone-16-Pro"))
        let iphone15 = try XCTUnwrap(catalog.deviceType(identifier: "com.apple.CoreSimulator.SimDeviceType.iPhone-15"))
        XCTAssertFalse(catalog.supports(runtime: ios175, deviceType: pro), "iPhone 16 Pro does not exist on iOS 17.5")
        XCTAssertTrue(catalog.supports(runtime: ios175, deviceType: iphone15))
        XCTAssertEqual(catalog.deviceTypes(for: ios175).count, 3)
    }

    func testDoctor() throws {
        let report = try Fixture.decode(DoctorReport.self, "doctor")
        XCTAssertEqual(report.status, .degraded)
        XCTAssertEqual(report.mode, .demo)
        XCTAssertNotNil(report.generatedAt)
        let mode = try XCTUnwrap(report.checks.first { $0.id == "mode" })
        XCTAssertEqual(mode.status, .warn)
        XCTAssertNotNil(mode.remedy)
        XCTAssertEqual(report.checks.filter { $0.status == .skip }.count, 4)
        XCTAssertNil(report.checks.first { $0.id == "capacity" }?.remedy)
    }

    func testDevices() throws {
        let empty = try Fixture.decode(DevicesResponse.self, "devices-empty")
        XCTAssertTrue(empty.items.isEmpty)
        XCTAssertEqual(empty.capacity?.load, 0)

        let response = try Fixture.decode(DevicesResponse.self, "devices")
        let device = try XCTUnwrap(response.items.first)
        XCTAssertEqual(device.name, "iPhone 15 Test")
        XCTAssertEqual(device.status, .ready)
        XCTAssertEqual(device.type, .simulator)
        XCTAssertEqual(device.backend, .simctl)
        XCTAssertTrue(device.canRunTests)
        XCTAssertFalse(device.ephemeral)
        XCTAssertEqual(device.runtimeDisplayName, "iOS 18.0")
        XCTAssertEqual(device.runtime, "com.apple.CoreSimulator.SimRuntime.iOS-18-0")
        XCTAssertEqual(device.modelName, "iPhone 15")
        XCTAssertNotNil(device.simulatorUdid)
        XCTAssertNotNil(device.createdAt)
        XCTAssertEqual(response.capacity, CapacitySnapshot(maxLoad: 4, load: 1, cpuCores: 4, memoryGb: 15.7))
        XCTAssertEqual(response.capacity?.fraction, 0.25)
    }

    func testEphemeralBusyDevice() throws {
        let response = try Fixture.decode(DevicesResponse.self, "devices-ephemeral")
        let auto = try XCTUnwrap(response.items.first { $0.ephemeral })
        XCTAssertEqual(auto.status, .busy)
        XCTAssertNotNil(auto.currentJobId)
        XCTAssertEqual(auto.name, "iPhone 15 (iOS 17.5)")
    }

    func testVirtualMachineDevice() throws {
        let response = try Fixture.decode(DevicesResponse.self, "devices-vm")
        let vm = try XCTUnwrap(response.items.first)
        XCTAssertTrue(vm.isVM)
        XCTAssertEqual(vm.backend, .simulated)
        XCTAssertFalse(vm.canRunTests)
        XCTAssertEqual(vm.cpu, 4)
        XCTAssertEqual(vm.screen, "1170x2532")
        XCTAssertEqual(vm.backupList, ["Clean Install"])
        XCTAssertEqual(vm.runtimeDisplayName, "simulated")
        XCTAssertEqual(response.capacity?.load, 4)
    }

    func testJobs() throws {
        let response = try Fixture.decode(JobsResponse.self, "tests")
        XCTAssertGreaterThanOrEqual(response.items.count, 8)

        let passing = try Fixture.job("DemoApp", status: .completed)
        XCTAssertEqual(passing.summary?.total, 8)
        XCTAssertEqual(passing.summary?.passed, 8)
        XCTAssertEqual(passing.exitCode, 0)
        XCTAssertTrue(passing.status.isTerminal)

        let failing = try Fixture.job("LoginFailTests")
        XCTAssertEqual(failing.status, .failed)
        XCTAssertEqual(failing.exitCode, 65)
        XCTAssertEqual(failing.error, "2 of 8 tests failed")
        XCTAssertEqual(failing.summary?.failed, 2)
        XCTAssertEqual(failing.summary?.buildFailed, false)

        let missing = try Fixture.job("MissingScheme")
        XCTAssertEqual(missing.summary?.buildFailed, true)
        XCTAssertEqual(missing.summary?.errors.count, 1)
        XCTAssertTrue(missing.summary?.errors.first?.contains("does not contain a scheme named \"MissingScheme\"") ?? false)

        let cancelled = try Fixture.job("SlowSuite")
        XCTAssertEqual(cancelled.status, .cancelled)
        XCTAssertEqual(cancelled.error, "Cancelled.")

        let flaky = try Fixture.job("FlakyTests")
        XCTAssertEqual(flaky.attempts, 2)
        XCTAssertEqual(flaky.maxRetries, 2)
        XCTAssertEqual(flaky.retries, 1)
    }

    func testRetryingAndRunningJobs() throws {
        let retrying = try Fixture.decode(TestJob.self, "job-retrying")
        XCTAssertEqual(retrying.status, .retrying)
        XCTAssertEqual(retrying.attemptText(), "attempt 2 of 3")
        XCTAssertNotNil(retrying.waitingReason)
        XCTAssertTrue(retrying.status.isActive)

        let running = try Fixture.decode(TestJob.self, "job-running")
        XCTAssertEqual(running.status, .running)
        XCTAssertNil(running.finishedAt)
        XCTAssertNil(running.summary)
        XCTAssertNotNil(running.startedAt)
    }

    func testJobEnvelopes() throws {
        let done = try Fixture.decode(JobEnvelope.self, "run-pass")
        XCTAssertEqual(done.job.status, .completed)
        XCTAssertEqual(done.scheduled, true)
        let queued = try Fixture.decode(JobEnvelope.self, "run-slow-queued")
        XCTAssertEqual(queued.job.testTarget, "SlowSuite")
    }

    func testRuns() throws {
        let runs = try Fixture.decode(RunsResponse.self, "runs").items
        XCTAssertEqual(runs.count, 2)
        let matrix = try XCTUnwrap(runs.first { $0.name == "Matrix" })
        XCTAssertEqual(matrix.status, .passed)
        XCTAssertEqual(matrix.counts.completed, 4)
        XCTAssertEqual(matrix.counts.total, 4)
        XCTAssertEqual(matrix.maxParallel, 2)
        XCTAssertEqual(matrix.jobIds.count, 4)
        XCTAssertNotNil(matrix.finishedAt)
        XCTAssertEqual(matrix.title, "Matrix")
        let unnamed = try XCTUnwrap(runs.first { $0.name == nil })
        XCTAssertEqual(unnamed.title, "DemoApp", "an unnamed run is called by its scheme")
    }

    func testRunDetailAndCreate() throws {
        let detail = try Fixture.decode(RunDetail.self, "run-detail")
        XCTAssertEqual(detail.jobs.count, 4)
        XCTAssertEqual(Set(detail.jobs.map(\.id)), Set(detail.run.jobIds))
        XCTAssertTrue(detail.jobs.allSatisfy { $0.runId == detail.run.id })

        let created = try Fixture.decode(CreateRunResponse.self, "run-create")
        XCTAssertEqual(created.run.status, .running)
        XCTAssertEqual(created.jobs.count, 4)
        XCTAssertTrue(created.skipped.isEmpty)
        XCTAssertTrue(created.jobs.contains { $0.status == .queued && $0.waitingReason != nil })

        let skipped = try Fixture.decode(CreateRunResponse.self, "run-skipped")
        XCTAssertEqual(skipped.jobs.count, 1)
        XCTAssertEqual(skipped.skipped, [SkippedCombination(runtime: "iOS 17.5", model: "iPhone 16 Pro", reason: "iPhone 16 Pro is not available on iOS 17.5")])
    }

    func testResults() throws {
        let failing = try Fixture.decode(JobResults.self, "results-fail")
        XCTAssertEqual(failing.attempt, 1)
        XCTAssertEqual(failing.exitCode, 65)
        XCTAssertEqual(failing.cases.count, 8)
        XCTAssertEqual(failing.device?.name, "iPhone 15 Test")
        XCTAssertEqual(failing.summary?.failed, 2)
        let failed = failing.cases.filter { $0.status == .failed }
        XCTAssertEqual(failed.map(\.name), ["testInvalidPassword", "testPaymentDeclined"])
        XCTAssertEqual(failed.first?.message, "LoginTests.swift:42 XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")")
        XCTAssertEqual(failed.first?.suiteName, "LoginTests")
        XCTAssertEqual(failed.first?.className, "LoginFailTestsTests.LoginTests")

        let missing = try Fixture.decode(JobResults.self, "results-missing")
        XCTAssertTrue(missing.cases.isEmpty)
        XCTAssertEqual(missing.summary?.buildFailed, true)
    }

    func testResultsOfJobWithoutResultsDecode() throws {
        // What the backend answers for a job that has not produced results: { attempt: 0, cases: [], summary: null }
        let data = Data(#"{"attempt":0,"cases":[],"summary":null}"#.utf8)
        let results = try BackendJSON.decoder().decode(JobResults.self, from: data)
        XCTAssertTrue(results.isEmpty)
        XCTAssertEqual(results.attempt, 0)
    }

    func testOutputAndArtifacts() throws {
        let output = try Fixture.decode(JobOutput.self, "output-fail")
        XCTAssertFalse(output.truncated)
        XCTAssertEqual(output.attempt, 1)
        XCTAssertTrue(output.text.hasPrefix("# MobileLab job "))
        XCTAssertTrue(output.text.contains("** TEST FAILED **"))
        XCTAssertGreaterThan(output.sizeBytes, 500)

        let artifacts = try Fixture.decode(ArtifactsResponse.self, "artifacts-pass").items
        XCTAssertEqual(artifacts.map(\.type), [.log, .results, .xcresult])
        XCTAssertEqual(artifacts.last?.isDirectory, true)
        XCTAssertNil(artifacts.last?.downloadUrl, "folders cannot be downloaded")
        XCTAssertNotNil(artifacts.first?.downloadUrl)
        XCTAssertTrue(artifacts.first?.downloadUrl?.hasPrefix("/artifacts/") ?? false)
    }

    func testMetrics() throws {
        let metrics = try Fixture.decode(MetricsSummary.self, "metrics-summary")
        XCTAssertGreaterThan(metrics.jobs, 5)
        XCTAssertNotNil(metrics.process)
        XCTAssertGreaterThan(metrics.process?.rssBytes ?? 0, 10_000_000)
        XCTAssertEqual(metrics.capacity?.maxLoad, 4)
        XCTAssertNotNil(metrics.jobsByStatus["completed"])
        XCTAssertGreaterThan(metrics.artifactBytes, 0)
        XCTAssertNotNil(metrics.hostMemoryFreeGb)
    }

    func testMaintenanceResults() throws {
        let cleanup = try Fixture.decode(CleanupResult.self, "cleanup")
        XCTAssertEqual(cleanup.jobsRemoved, 0)
        let sync = try Fixture.decode(SyncResult.self, "devices-sync")
        XCTAssertEqual(sync.removed, 0)
    }

    func testEvents() throws {
        let response = try Fixture.decode(EventsResponse.self, "events")
        XCTAssertFalse(response.items.isEmpty)
        XCTAssertNotNil(response.lastId)
        XCTAssertTrue(response.items.allSatisfy { !$0.isOutput }, "the history never contains raw output")
        XCTAssertEqual(response.items.map(\.id), response.items.map(\.id).sorted())

        let jobEvents = try Fixture.decode(EventsResponse.self, "events-job").items
        XCTAssertTrue(jobEvents.allSatisfy { $0.jobId != nil })
        XCTAssertTrue(jobEvents.contains { $0.action == "job_finished" && $0.type == "error" })
    }

    func testStreamedEventsIncludeOutputAndOddMessages() throws {
        let events = try Fixture.streamedEvents()
        XCTAssertGreaterThan(events.count, 20)
        let output = events.filter(\.isOutput)
        XCTAssertGreaterThan(output.count, 15)
        XCTAssertTrue(output.contains { $0.message.hasPrefix("Command line invocation:\n") }, "one output event can hold several lines")
        XCTAssertTrue(output.contains { $0.message == "\n** TEST FAILED **\n" })
        XCTAssertTrue(events.contains { !$0.isOutput && $0.action == "job_finished" })
    }

    func testErrorBodies() throws {
        struct Body: Decodable { var error: String; var message: String }
        for name in ["error-400", "error-401", "error-404", "error-409", "error-429"] {
            let body = try Fixture.decode(Body.self, name)
            XCTAssertFalse(body.message.isEmpty, name)
        }
    }

    func testAllJSONFixturesAreValidJSON() throws {
        let folder = try Fixture.url("health.json").deletingLastPathComponent()
        let files = try FileManager.default.contentsOfDirectory(atPath: folder.path).filter { $0.hasSuffix(".json") }
        XCTAssertGreaterThan(files.count, 40)
        for file in files {
            XCTAssertNoThrow(try JSONSerialization.jsonObject(with: Fixture.data(file)), file)
        }
    }

    func testScreenshotIsAPNG() throws {
        let data = try Fixture.data("screenshot.png")
        XCTAssertEqual(Array(data.prefix(8)), [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
    }
}
