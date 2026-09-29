import Foundation

public enum FindScope: String, CaseIterable, Codable, Sendable, Identifiable {
    case devices
    case tests
    case events
    case log

    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .devices: return "Devices"
        case .tests: return "Tests"
        case .events: return "Events"
        case .log: return "Log of Open Job"
        }
    }
}

public struct FindMatch: Equatable, Sendable, Identifiable {
    public var id: String
    public var text: String
    /// The matched part of `text`, in characters, for highlighting.
    public var range: Range<Int>
    public var target: EditorTarget?
    public var tab: ReportTab?
    /// 1-based log line to scroll to.
    public var line: Int?
    public var focus: String?
}

public struct FindGroup: Equatable, Sendable, Identifiable {
    public var id: String
    public var title: String
    public var icon: NodeIcon
    public var matches: [FindMatch]
}

/// The Find navigator: plain case-insensitive search over what the app already holds.
public enum FindEngine {
    public static let matchLimit = 300

    public static func search(
        query: String, scope: FindScope, state: DashboardState, log: LogDocument?, openJobID: String?, catalog: Catalog? = nil,
        formatting: DateFormatting
    ) -> [FindGroup] {
        let needle = query.trimmingCharacters(in: .whitespaces)
        guard !needle.isEmpty else { return [] }

        switch scope {
        case .devices: return devices(needle, state)
        case .tests: return tests(needle, state, catalog)
        case .events: return events(needle, state, formatting)
        case .log: return logLines(needle, log, openJobID, state, catalog)
        }
    }

    static func range(of needle: String, in text: String) -> Range<Int>? {
        guard let found = text.range(of: needle, options: [.caseInsensitive, .diacriticInsensitive]) else { return nil }
        let start = text.distance(from: text.startIndex, to: found.lowerBound)
        let length = text.distance(from: found.lowerBound, to: found.upperBound)
        return start..<(start + length)
    }

    private static func devices(_ needle: String, _ state: DashboardState) -> [FindGroup] {
        var matches: [FindMatch] = []
        for device in state.devices {
            let label = "\(device.name) (\(device.runtimeDisplayName))"
            let haystacks = [label, device.simulatorUdid ?? "", device.id]
            guard let hit = haystacks.enumerated().first(where: { range(of: needle, in: $0.element) != nil }) else { continue }
            let text = hit.offset == 0 ? label : "\(label)  \(haystacks[hit.offset])"
            matches.append(FindMatch(
                id: "find:device:\(device.id)", text: text, range: range(of: needle, in: text) ?? 0..<0, target: .device(device.id)
            ))
            if matches.count >= matchLimit { break }
        }
        return matches.isEmpty ? [] : [FindGroup(id: "find:devices", title: "Devices", icon: .simulators, matches: matches)]
    }

    private static func tests(_ needle: String, _ state: DashboardState, _ catalog: Catalog?) -> [FindGroup] {
        var groups: [FindGroup] = []
        var total = 0
        for job in state.jobs {
            var matches: [FindMatch] = []
            let title = JobText.title(job, catalog: catalog)
            if let r = range(of: needle, in: title) {
                matches.append(FindMatch(id: "find:job:\(job.id)", text: title, range: r, target: .job(job.id), tab: .summary))
            }
            for testCase in state.results[job.id]?.cases ?? [] {
                let label = "\(testCase.suiteName).\(testCase.name)"
                if let r = range(of: needle, in: label) {
                    matches.append(FindMatch(id: "find:case:\(job.id):\(testCase.id)", text: label, range: r, target: .job(job.id), tab: .tests))
                } else if let message = testCase.message, let r = range(of: needle, in: message) {
                    let location = FailureMessage.parse(message).location?.display
                    matches.append(FindMatch(id: "find:message:\(job.id):\(testCase.id)", text: message, range: r, target: .job(job.id), tab: .logs, focus: location))
                }
            }
            if matches.isEmpty { continue }
            groups.append(FindGroup(id: "find:group:\(job.id)", title: title, icon: .diamond(JobText.glyph(job.status)), matches: matches))
            total += matches.count
            if total >= matchLimit { break }
        }
        return groups
    }

    private static func events(_ needle: String, _ state: DashboardState, _ formatting: DateFormatting) -> [FindGroup] {
        var matches: [FindMatch] = []
        for event in state.activity.reversed() {
            guard let r = range(of: needle, in: event.message) else { continue }
            let stamp = formatting.clock(event.timestamp) + "  "
            let text = stamp + event.message
            let shifted = (r.lowerBound + stamp.count)..<(r.upperBound + stamp.count)
            matches.append(FindMatch(
                id: "find:event:\(event.dedupeKey)", text: text, range: shifted,
                target: event.jobId.map { .job($0) } ?? event.deviceId.map { .device($0) } ?? event.runId.map { .run($0) }
            ))
            if matches.count >= matchLimit { break }
        }
        return matches.isEmpty ? [] : [FindGroup(id: "find:events", title: "Events", icon: .document, matches: matches)]
    }

    private static func logLines(_ needle: String, _ log: LogDocument?, _ openJobID: String?, _ state: DashboardState, _ catalog: Catalog?) -> [FindGroup] {
        guard let log, let openJobID else { return [] }
        var matches: [FindMatch] = []
        for line in log.lines {
            guard let r = range(of: needle, in: line.text) else { continue }
            matches.append(FindMatch(
                id: "find:line:\(openJobID):\(line.number)", text: line.text, range: r, target: .job(openJobID), tab: .logs, line: line.number
            ))
            if matches.count >= matchLimit { break }
        }
        if matches.isEmpty { return [] }
        let title = state.job(id: openJobID).map { JobText.title($0, catalog: catalog) } ?? "Open job"
        return [FindGroup(id: "find:log:\(openJobID)", title: title, icon: .document, matches: matches)]
    }
}
