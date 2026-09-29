import Foundation
import XCTest
@testable import IOSLabDashboardCore

/// Loads the JSON the real backend produced (see Fixtures/capture.sh).
enum Fixture {
    static func url(_ name: String) throws -> URL {
        let base = Bundle.module.resourceURL!.appendingPathComponent("Fixtures")
        let url = base.appendingPathComponent(name)
        guard FileManager.default.fileExists(atPath: url.path) else { throw FixtureError.missing(name) }
        return url
    }

    static func data(_ file: String) throws -> Data { try Data(contentsOf: url(file)) }
    static func text(_ file: String) throws -> String { String(decoding: try data(file), as: UTF8.self) }

    /// `name` without extension: `devices` loads `devices.json`.
    static func decode<T: Decodable>(_ type: T.Type, _ name: String) throws -> T {
        try BackendJSON.decoder().decode(T.self, from: data(name + ".json"))
    }

    /// The lines of `events-ws.ndjson` as decoded events.
    static func streamedEvents() throws -> [EngineEvent] {
        try text("events-ws.ndjson").split(separator: "\n").map { try BackendJSON.decoder().decode(EngineEvent.self, from: Data($0.utf8)) }
    }

    static func job(_ scheme: String, status: JobStatus? = nil) throws -> TestJob {
        let jobs = try decode(JobsResponse.self, "tests").items
        guard let job = jobs.first(where: { $0.testTarget == scheme && (status == nil || $0.status == status) }) else { throw FixtureError.missing("job for \(scheme)") }
        return job
    }
}

enum FixtureError: Error { case missing(String) }

func date(_ iso: String, file: StaticString = #filePath, line: UInt = #line) -> Date {
    guard let value = ISO8601.parse(iso) else {
        XCTFail("bad test date \(iso)", file: file, line: line)
        return Date(timeIntervalSince1970: 0)
    }
    return value
}

/// UTC calendar so that "Today at 9:41 AM" does not depend on the machine running the tests.
let utcFormatting: DateFormatting = {
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(identifier: "UTC")!
    return DateFormatting(calendar: calendar, use24Hour: false)
}()

func makeJob(
    _ id: String = "job-1", scheme: String = "DemoApp", status: JobStatus, created: String = "2026-09-29T09:00:00Z",
    started: String? = nil, finished: String? = nil, attempts: Int = 1, maxRetries: Int = 0, device: String? = "iPhone 15",
    summary: JobTestSummary? = nil, error: String? = nil, waiting: String? = nil, runId: String? = nil, durationMs: Double? = nil
) -> TestJob {
    TestJob(
        id: id, runId: runId, testTarget: scheme, status: status, retries: max(0, attempts - 1), maxRetries: maxRetries, attempts: attempts,
        assignedDeviceName: device, waitingReason: waiting, error: error, summary: summary,
        startedAt: started.map { date($0) }, finishedAt: finished.map { date($0) }, durationMs: durationMs, createdAt: date(created)
    )
}
