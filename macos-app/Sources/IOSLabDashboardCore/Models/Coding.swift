import Foundation

/// ISO 8601 timestamps as the backend writes them (`2026-09-29T16:10:02.277Z`).
///
/// Parsed by hand instead of with `ISO8601DateFormatter`: it is deterministic on every platform, does not
/// depend on ICU, and is cheap enough to call for every event of a busy stream.
public enum ISO8601 {
    /// Accepts `YYYY-MM-DDTHH:MM:SS`, an optional fraction, and `Z` or `+HH:MM` / `-HH:MM` (no zone means UTC).
    public static func parse(_ text: String) -> Date? {
        let s = Array(text.utf8)
        var i = 0

        func digits(_ count: Int) -> Int? {
            guard i + count <= s.count else { return nil }
            var value = 0
            for k in 0..<count {
                let c = s[i + k]
                guard c >= 48 && c <= 57 else { return nil }
                value = value * 10 + Int(c - 48)
            }
            i += count
            return value
        }
        func expect(_ char: UInt8) -> Bool {
            guard i < s.count, s[i] == char else { return false }
            i += 1
            return true
        }

        guard let year = digits(4), expect(45), let month = digits(2), expect(45), let day = digits(2) else { return nil }
        guard expect(84) || expect(32) else { return nil }  // "T" or " "
        guard let hour = digits(2), expect(58), let minute = digits(2), expect(58), let second = digits(2) else { return nil }
        guard (1...12).contains(month), (1...31).contains(day), hour < 24, minute < 60, second < 61 else { return nil }

        var fraction = 0.0
        if i < s.count, s[i] == 46 {  // "."
            i += 1
            var scale = 0.1
            var any = false
            while i < s.count, s[i] >= 48, s[i] <= 57 {
                fraction += Double(s[i] - 48) * scale
                scale /= 10
                i += 1
                any = true
            }
            if !any { return nil }
        }

        var offsetSeconds = 0
        if i < s.count {
            if s[i] == 90 {  // "Z"
                i += 1
            } else if s[i] == 43 || s[i] == 45 {
                let sign = s[i] == 45 ? -1 : 1
                i += 1
                guard let oh = digits(2) else { return nil }
                _ = expect(58)
                let om = digits(2) ?? 0
                offsetSeconds = sign * (oh * 3600 + om * 60)
            } else {
                return nil
            }
        }
        guard i == s.count else { return nil }

        let days = daysFromCivil(year: year, month: month, day: day)
        let seconds = Double(days) * 86_400 + Double(hour * 3600 + minute * 60 + second - offsetSeconds) + fraction
        return Date(timeIntervalSince1970: seconds)
    }

    /// `2026-09-29T16:10:02.277Z`, always UTC with milliseconds.
    public static func string(from date: Date) -> String {
        let millisTotal = Int((date.timeIntervalSince1970 * 1000).rounded(.down))
        var days = millisTotal / 86_400_000
        var rest = millisTotal % 86_400_000
        if rest < 0 {
            rest += 86_400_000
            days -= 1
        }
        let (y, m, d) = civilFromDays(days)
        let hour = rest / 3_600_000
        let minute = (rest / 60_000) % 60
        let second = (rest / 1000) % 60
        let millis = rest % 1000
        return "\(pad(y, 4))-\(pad(m, 2))-\(pad(d, 2))T\(pad(hour, 2)):\(pad(minute, 2)):\(pad(second, 2)).\(pad(millis, 3))Z"
    }

    private static func pad(_ value: Int, _ width: Int) -> String {
        let text = String(value)
        return text.count >= width ? text : String(repeating: "0", count: width - text.count) + text
    }

    /// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
    static func daysFromCivil(year: Int, month: Int, day: Int) -> Int {
        let y = month <= 2 ? year - 1 : year
        let era = (y >= 0 ? y : y - 399) / 400
        let yoe = y - era * 400
        let mp = (month + 9) % 12
        let doy = (153 * mp + 2) / 5 + day - 1
        let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
        return era * 146_097 + doe - 719_468
    }

    static func civilFromDays(_ z0: Int) -> (Int, Int, Int) {
        let z = z0 + 719_468
        let era = (z >= 0 ? z : z - 146_096) / 146_097
        let doe = z - era * 146_097
        let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365
        let y = yoe + era * 400
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
        let mp = (5 * doy + 2) / 153
        let d = doy - (153 * mp + 2) / 5 + 1
        let m = mp < 10 ? mp + 3 : mp - 9
        return (m <= 2 ? y + 1 : y, m, d)
    }
}

/// Encoders and decoders configured the way the backend speaks JSON.
public enum BackendJSON {
    public static func decoder() -> JSONDecoder {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .custom { decoder in
            let container = try decoder.singleValueContainer()
            let text = try container.decode(String.self)
            guard let date = ISO8601.parse(text) else {
                throw DecodingError.dataCorruptedError(in: container, debugDescription: "Not an ISO 8601 date: \(text)")
            }
            return date
        }
        return decoder
    }

    public static func encoder() -> JSONEncoder {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        encoder.dateEncodingStrategy = .custom { date, encoder in
            var container = encoder.singleValueContainer()
            try container.encode(ISO8601.string(from: date))
        }
        return encoder
    }
}

/// Free-form JSON, used for the `metadata` bag of events.
public enum JSONValue: Codable, Equatable, Sendable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    public init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self = .null
        } else if let value = try? container.decode(Bool.self) {
            self = .bool(value)
        } else if let value = try? container.decode(Double.self) {
            self = .number(value)
        } else if let value = try? container.decode(String.self) {
            self = .string(value)
        } else if let value = try? container.decode([JSONValue].self) {
            self = .array(value)
        } else if let value = try? container.decode([String: JSONValue].self) {
            self = .object(value)
        } else {
            throw DecodingError.dataCorruptedError(in: container, debugDescription: "Unsupported JSON value")
        }
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .null: try container.encodeNil()
        case .bool(let value): try container.encode(value)
        case .number(let value): try container.encode(value)
        case .string(let value): try container.encode(value)
        case .array(let value): try container.encode(value)
        case .object(let value): try container.encode(value)
        }
    }

    public var stringValue: String? {
        if case .string(let value) = self { return value }
        return nil
    }
}
