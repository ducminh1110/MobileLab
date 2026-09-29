import XCTest
@testable import IOSLabDashboardCore

final class CodingTests: XCTestCase {
    func testParsesBackendTimestamps() throws {
        let d = try XCTUnwrap(ISO8601.parse("2026-09-29T16:10:02.277Z"))
        XCTAssertEqual(d.timeIntervalSince1970, 1_790_698_202.277, accuracy: 0.0005)
        XCTAssertEqual(ISO8601.parse("1970-01-01T00:00:00Z")?.timeIntervalSince1970, 0)
        XCTAssertEqual(ISO8601.parse("2000-02-29T12:00:00Z")?.timeIntervalSince1970, 951_825_600, "leap day")
        XCTAssertEqual(ISO8601.parse("2026-09-29 16:10:02")?.timeIntervalSince1970, 1_790_698_202, "space separator, no zone means UTC")
    }

    func testTimeZoneOffsets() {
        let utc = ISO8601.parse("2026-09-29T16:00:00Z")
        XCTAssertEqual(ISO8601.parse("2026-09-29T18:00:00+02:00"), utc)
        XCTAssertEqual(ISO8601.parse("2026-09-29T11:30:00-04:30"), utc)
        XCTAssertEqual(ISO8601.parse("2026-09-29T18:00:00+0200"), utc)
    }

    func testRejectsGarbage() {
        for bad in ["", "yesterday", "2026-13-01T00:00:00Z", "2026-09-29", "2026-09-29T25:00:00Z", "2026-09-29T10:00:00.Z", "2026-09-29T10:00:00Zjunk", "2026/09/29T10:00:00Z"] {
            XCTAssertNil(ISO8601.parse(bad), bad)
        }
    }

    func testRoundTripAcrossManyDates() throws {
        var seconds = -2_000_000_000.0
        while seconds < 4_100_000_000 {
            let date = Date(timeIntervalSince1970: seconds + 0.123)
            let text = ISO8601.string(from: date)
            let parsed = try XCTUnwrap(ISO8601.parse(text), text)
            XCTAssertEqual(parsed.timeIntervalSince1970, date.timeIntervalSince1970, accuracy: 0.0011, text)
            seconds += 86_400 * 37 + 3_601
        }
    }

    func testFormatsKnownInstants() {
        XCTAssertEqual(ISO8601.string(from: Date(timeIntervalSince1970: 0)), "1970-01-01T00:00:00.000Z")
        XCTAssertEqual(ISO8601.string(from: Date(timeIntervalSince1970: 1_790_698_202.277)), "2026-09-29T16:10:02.277Z")
        XCTAssertEqual(ISO8601.string(from: Date(timeIntervalSince1970: -1.5)), "1969-12-31T23:59:58.500Z")
    }

    func testAgreesWithFoundationsOwnParser() throws {
        // Cross-check against ISO8601DateFormatter (which the app deliberately does not use) on real fixture timestamps.
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        for event in try Fixture.decode(EventsResponse.self, "events").items {
            let text = ISO8601.string(from: event.timestamp)
            let foundation = try XCTUnwrap(formatter.date(from: text))
            XCTAssertEqual(foundation.timeIntervalSince1970, event.timestamp.timeIntervalSince1970, accuracy: 0.001)
        }
    }

    func testDecoderAndEncoderAgree() throws {
        struct Holder: Codable, Equatable { var at: Date }
        let original = Holder(at: date("2026-09-29T16:10:02.277Z"))
        let data = try BackendJSON.encoder().encode(original)
        XCTAssertEqual(String(decoding: data, as: UTF8.self), #"{"at":"2026-09-29T16:10:02.277Z"}"#)
        XCTAssertEqual(try BackendJSON.decoder().decode(Holder.self, from: data), original)
        XCTAssertThrowsError(try BackendJSON.decoder().decode(Holder.self, from: Data(#"{"at":"soon"}"#.utf8)))
    }

    func testJSONValue() throws {
        let json = #"{"a":1,"b":"x","c":[true,null,2.5],"d":{"e":false}}"#
        let value = try JSONDecoder().decode(JSONValue.self, from: Data(json.utf8))
        guard case .object(let object) = value else { return XCTFail("not an object") }
        XCTAssertEqual(object["a"], .number(1))
        XCTAssertEqual(object["b"]?.stringValue, "x")
        XCTAssertEqual(object["c"], .array([.bool(true), .null, .number(2.5)]))
        XCTAssertEqual(object["d"], .object(["e": .bool(false)]))
        let again = try JSONDecoder().decode(JSONValue.self, from: JSONEncoder().encode(value))
        XCTAssertEqual(again, value)
    }

    func testEventWithMetadataDecodes() throws {
        let json = #"{"id":7,"source":"scheduler","type":"log","action":"x","message":"m","timestamp":"2026-09-29T16:10:02.277Z","metadata":{"attempt":2,"tags":["a"]}}"#
        let event = try BackendJSON.decoder().decode(EngineEvent.self, from: Data(json.utf8))
        XCTAssertEqual(event.metadata?["attempt"], .number(2))
        XCTAssertEqual(event.metadata?["tags"], .array([.string("a")]))
    }

    func testUnknownEnumValuesDoNotFailDecoding() throws {
        let job = #"{"id":"j","testTarget":"S","status":"paused","createdAt":"2026-09-29T16:10:02.277Z"}"#
        XCTAssertEqual(try BackendJSON.decoder().decode(TestJob.self, from: Data(job.utf8)).status, .unknown)
        let device = #"{"id":"d","name":"N","status":"hibernating","type":"hologram","backend":"quantum","runtime":"r"}"#
        let decoded = try BackendJSON.decoder().decode(Device.self, from: Data(device.utf8))
        XCTAssertEqual(decoded.status, .unknown)
        XCTAssertEqual(decoded.type, .unknown)
        XCTAssertEqual(decoded.backend, .unknown)
    }

    func testMissingOptionalFieldsUseSafeDefaults() throws {
        let minimal = try BackendJSON.decoder().decode(TestJob.self, from: Data(#"{"id":"j","createdAt":"2026-09-29T16:10:02Z"}"#.utf8))
        XCTAssertEqual(minimal.retries, 0)
        XCTAssertEqual(minimal.attempts, 0)
        XCTAssertTrue(minimal.autoProvision)
        XCTAssertEqual(minimal.updatedAt, minimal.createdAt)
        let counts = try JSONDecoder().decode(JobCounts.self, from: Data(#"{"running":2}"#.utf8))
        XCTAssertEqual(counts.total, 2)
        let metrics = try JSONDecoder().decode(MetricsSummary.self, from: Data(#"{"devices":1,"jobs":2}"#.utf8))
        XCTAssertNil(metrics.process, "an older backend sends no process block")
    }

    func testRuntimeNames() {
        XCTAssertEqual(RuntimeName.display(fromIdentifier: "com.apple.CoreSimulator.SimRuntime.iOS-18-0"), "iOS 18.0")
        XCTAssertEqual(RuntimeName.display(fromIdentifier: "com.apple.CoreSimulator.SimRuntime.iOS-17-5-1"), "iOS 17.5.1")
        XCTAssertEqual(RuntimeName.display(fromIdentifier: "simulated"), "simulated")
        XCTAssertEqual(RuntimeName.versionComponents("iOS 17.5"), [17, 5])
        XCTAssertEqual(RuntimeName.versionComponents("com.apple.CoreSimulator.SimRuntime.iOS-18-2"), [18, 2])
        XCTAssertTrue(RuntimeName.versionComponents("iOS 17.5").lexicographicallyPrecedes(RuntimeName.versionComponents("iOS 18.0")))
    }

    func testDevicePredicates() throws {
        let devices = try Fixture.decode(DevicesResponse.self, "devices-after").items
        let ready = try XCTUnwrap(devices.first { $0.status == .ready })
        XCTAssertTrue(ready.status.isBooted)
        XCTAssertFalse(ready.isTablet)
        let ipad = Device(id: "1", name: "Lab iPad", runtime: "r", modelName: "iPad (10th generation)", status: .stopped)
        XCTAssertTrue(ipad.isTablet)
        XCTAssertEqual(StatusDot(.shuttingDown), .shuttingDown)
        XCTAssertTrue(StatusDot.booting.pulses)
        XCTAssertFalse(StatusDot.ready.pulses)
    }
}
