import XCTest
@testable import IOSLabDashboardCore

final class FormattingTests: XCTestCase {
    func testElapsedClock() {
        XCTAssertEqual(Formatters.elapsed(0), "00:00")
        XCTAssertEqual(Formatters.elapsed(7.9), "00:07")
        XCTAssertEqual(Formatters.elapsed(65), "01:05")
        XCTAssertEqual(Formatters.elapsed(3599), "59:59")
        XCTAssertEqual(Formatters.elapsed(3723), "1:02:03")
        XCTAssertEqual(Formatters.elapsed(-5), "00:00", "a clock skew must not print a negative time")
    }

    func testDurations() {
        XCTAssertEqual(Formatters.duration(0), "0 s")
        XCTAssertEqual(Formatters.duration(0.02), "0.020 s")
        XCTAssertEqual(Formatters.duration(0.2), "0.200 s")
        XCTAssertEqual(Formatters.duration(1.239), "1.24 s")
        XCTAssertEqual(Formatters.duration(12.44), "12.4 s")
        XCTAssertEqual(Formatters.duration(59.9), "59.9 s")
        XCTAssertEqual(Formatters.duration(60), "1 min 00 s")
        XCTAssertEqual(Formatters.duration(125), "2 min 05 s")
        XCTAssertEqual(Formatters.duration(3725), "1 h 02 min")
        XCTAssertEqual(Formatters.duration(-1), "0 s")
    }

    func testBytes() {
        XCTAssertEqual(Formatters.bytes(0), "0 bytes")
        XCTAssertEqual(Formatters.bytes(1), "1 byte")
        XCTAssertEqual(Formatters.bytes(1023), "1023 bytes")
        XCTAssertEqual(Formatters.bytes(1024), "1.0 KB")
        XCTAssertEqual(Formatters.bytes(1_638), "1.6 KB")
        XCTAssertEqual(Formatters.bytes(108_000_000), "103 MB")
        XCTAssertEqual(Formatters.bytes(41_500_000), "39.6 MB")
        XCTAssertEqual(Formatters.bytes(3 * 1024 * 1024 * 1024), "3.0 GB")
        XCTAssertEqual(Formatters.bytes(5 * 1024 * 1024 * 1024 * 1024), "5.0 TB")
        XCTAssertEqual(Formatters.bytes(-4), "0 bytes")
    }

    func testPercentAndPlural() {
        XCTAssertEqual(Formatters.percent(1.5), "1.5%")
        XCTAssertEqual(Formatters.percent(0), "0.0%")
        XCTAssertEqual(Formatters.plural(1, "simulator"), "1 simulator")
        XCTAssertEqual(Formatters.plural(0, "simulator"), "0 simulators")
        XCTAssertEqual(Formatters.plural(2, "test"), "2 tests")
        XCTAssertEqual(Formatters.plural(2, "match", plural: "matches"), "2 matches")
        XCTAssertEqual(Formatters.shortID("3fa85f64-5717-4562-b3fc-2c963f66afa6"), "3fa85f64")
    }

    func testTimeOfDay() {
        XCTAssertEqual(utcFormatting.time(date("2026-09-29T09:41:00Z")), "9:41 AM")
        XCTAssertEqual(utcFormatting.time(date("2026-09-29T00:05:00Z")), "12:05 AM")
        XCTAssertEqual(utcFormatting.time(date("2026-09-29T12:00:00Z")), "12:00 PM")
        XCTAssertEqual(utcFormatting.time(date("2026-09-29T23:59:00Z")), "11:59 PM")
        var h24 = utcFormatting
        h24.use24Hour = true
        XCTAssertEqual(h24.time(date("2026-09-29T09:41:00Z")), "09:41")
        XCTAssertEqual(h24.time(date("2026-09-29T23:59:00Z")), "23:59")
        XCTAssertEqual(utcFormatting.clock(date("2026-09-29T09:41:07Z")), "09:41:07")
    }

    func testRelativeDays() {
        let now = date("2026-09-29T16:30:00Z")
        XCTAssertEqual(utcFormatting.day(date("2026-09-29T00:00:01Z"), now: now), "Today")
        XCTAssertEqual(utcFormatting.day(date("2026-09-28T23:59:59Z"), now: now), "Yesterday")
        XCTAssertEqual(utcFormatting.day(date("2026-09-27T12:00:00Z"), now: now), "Sep 27")
        XCTAssertEqual(utcFormatting.day(date("2025-12-31T12:00:00Z"), now: now), "Dec 31, 2025")
        XCTAssertEqual(utcFormatting.dayAndTime(date("2026-09-29T09:41:00Z"), now: now), "Today at 9:41 AM")
        XCTAssertEqual(utcFormatting.dayAndTime(date("2026-09-28T21:05:00Z"), now: now), "Yesterday at 9:05 PM")
    }

    func testMidnightBoundaryFollowsTheCalendarsTimeZone() {
        var tokyo = Calendar(identifier: .gregorian)
        tokyo.timeZone = TimeZone(identifier: "Asia/Tokyo")!
        let formatting = DateFormatting(calendar: tokyo)
        let now = date("2026-09-29T16:30:00Z")  // 01:30 on the 30th in Tokyo
        XCTAssertEqual(formatting.day(date("2026-09-29T16:00:00Z"), now: now), "Today")
        XCTAssertEqual(formatting.day(date("2026-09-29T14:00:00Z"), now: now), "Yesterday")
        XCTAssertEqual(formatting.time(date("2026-09-29T16:00:00Z")), "1:00 AM")
    }

    func testJobDurationPrefersTheBackendValueButFallsBackToTimestamps() {
        let now = date("2026-09-29T10:00:00Z")
        // The demo runner reports 0 ms for everything, so 0 must not win over real timestamps.
        let demo = makeJob(status: .completed, started: "2026-09-29T09:00:00Z", finished: "2026-09-29T09:00:12.500Z", durationMs: 0)
        XCTAssertEqual(demo.elapsedSeconds(now: now) ?? -1, 12.5, accuracy: 0.001)
        let real = makeJob(status: .completed, started: "2026-09-29T09:00:00Z", finished: "2026-09-29T09:00:12Z", durationMs: 11_800)
        XCTAssertEqual(real.elapsedSeconds(now: now) ?? -1, 11.8, accuracy: 0.001)
        let running = makeJob(status: .running, started: "2026-09-29T09:59:53Z")
        XCTAssertEqual(running.elapsedSeconds(now: now) ?? -1, 7, accuracy: 0.001)
        XCTAssertNil(makeJob(status: .queued, attempts: 0).elapsedSeconds(now: now))
    }

    func testAttemptText() {
        XCTAssertEqual(makeJob(status: .running, attempts: 1, maxRetries: 0).attemptText(), "attempt 1 of 1")
        XCTAssertEqual(makeJob(status: .running, attempts: 2, maxRetries: 2).attemptText(), "attempt 2 of 3")
        XCTAssertEqual(makeJob(status: .retrying, attempts: 1, maxRetries: 2).attemptText(), "attempt 2 of 3")
        XCTAssertEqual(makeJob(status: .retrying, attempts: 5, maxRetries: 2).attemptText(), "attempt 3 of 3", "never beyond the maximum")
    }
}
