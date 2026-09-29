import Foundation

/// Dates the way Xcode words them: `Today at 9:41 AM`, `Yesterday at 9:41 AM`, `Sep 27 at 9:41 AM`.
/// Built by hand from calendar components (English month names) so the output is identical in tests and in the app.
public struct DateFormatting: Sendable {
    public var calendar: Calendar
    public var use24Hour: Bool

    public init(calendar: Calendar = .current, use24Hour: Bool = false) {
        self.calendar = calendar
        self.use24Hour = use24Hour
    }

    static let months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    /// `9:41 AM` or `09:41`.
    public func time(_ date: Date) -> String {
        let parts = calendar.dateComponents([.hour, .minute], from: date)
        let hour = parts.hour ?? 0, minute = parts.minute ?? 0
        let mm = Formatters.twoDigits(minute)
        if use24Hour { return "\(Formatters.twoDigits(hour)):\(mm)" }
        let h12 = hour % 12 == 0 ? 12 : hour % 12
        return "\(h12):\(mm) \(hour < 12 ? "AM" : "PM")"
    }

    /// `09:41:07`, for console timestamps.
    public func clock(_ date: Date) -> String {
        let parts = calendar.dateComponents([.hour, .minute, .second], from: date)
        return "\(Formatters.twoDigits(parts.hour ?? 0)):\(Formatters.twoDigits(parts.minute ?? 0)):\(Formatters.twoDigits(parts.second ?? 0))"
    }

    /// `Today`, `Yesterday`, `Sep 27` (or `Sep 27, 2025` in another year).
    public func day(_ date: Date, now: Date) -> String {
        if calendar.isDate(date, inSameDayAs: now) { return "Today" }
        if let yesterday = calendar.date(byAdding: .day, value: -1, to: now), calendar.isDate(date, inSameDayAs: yesterday) { return "Yesterday" }
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        let nowYear = calendar.component(.year, from: now)
        let month = DateFormatting.months[max(0, min(11, (parts.month ?? 1) - 1))]
        let base = "\(month) \(parts.day ?? 1)"
        return parts.year == nowYear ? base : "\(base), \(parts.year ?? 0)"
    }

    /// `Today at 9:41 AM`.
    public func dayAndTime(_ date: Date, now: Date) -> String {
        "\(day(date, now: now)) at \(time(date))"
    }
}

public enum Formatters {
    static func twoDigits(_ value: Int) -> String { value < 10 ? "0\(value)" : "\(value)" }

    /// The running clock in the toolbar: `00:07`, `12:03`, `1:02:03`.
    public static func elapsed(_ seconds: TimeInterval) -> String {
        let total = max(0, Int(seconds))
        let h = total / 3600, m = (total / 60) % 60, s = total % 60
        if h > 0 { return "\(h):\(twoDigits(m)):\(twoDigits(s))" }
        return "\(twoDigits(m)):\(twoDigits(s))"
    }

    /// `0.020 s`, `1.24 s`, `12.4 s`, `2 min 03 s`, `1 h 02 min`.
    public static func duration(_ seconds: TimeInterval) -> String {
        let value = max(0, seconds)
        if value == 0 { return "0 s" }
        if value < 1 { return String(format: "%.3f s", value) }
        if value < 10 { return String(format: "%.2f s", value) }
        if value < 60 { return String(format: "%.1f s", value) }
        let total = Int(value.rounded())
        if total < 3600 { return "\(total / 60) min \(twoDigits(total % 60)) s" }
        return "\(total / 3600) h \(twoDigits((total / 60) % 60)) min"
    }

    /// `12 bytes`, `1.6 KB`, `103 MB`, `2.4 GB` (1024 based, like Xcode's memory gauge).
    public static func bytes(_ count: Int) -> String {
        let value = Double(max(0, count))
        if value < 1024 { return count == 1 ? "1 byte" : "\(max(0, count)) bytes" }
        let units = ["KB", "MB", "GB", "TB"]
        var scaled = value / 1024
        var unit = 0
        while scaled >= 1024, unit < units.count - 1 {
            scaled /= 1024
            unit += 1
        }
        let text = scaled >= 100 ? String(format: "%.0f", scaled) : String(format: "%.1f", scaled)
        return "\(text) \(units[unit])"
    }

    /// `1.5%` (one decimal, trailing `.0` kept so the gauge does not jitter in width).
    public static func percent(_ value: Double) -> String { String(format: "%.1f%%", max(0, value)) }

    public static func plural(_ count: Int, _ singular: String, plural: String? = nil) -> String {
        "\(count) \(count == 1 ? singular : (plural ?? singular + "s"))"
    }

    /// First eight characters of an id, enough to tell jobs apart.
    public static func shortID(_ id: String) -> String { String(id.prefix(8)) }
}
