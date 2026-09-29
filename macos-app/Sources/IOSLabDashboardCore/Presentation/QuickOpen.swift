import Foundation

/// Open Quickly (`Cmd Shift O`): jump to any device, run, job or test.
public struct QuickOpenItem: Equatable, Sendable, Identifiable {
    public enum Kind: String, Sendable { case device, run, job, test }

    public var id: String
    public var title: String
    public var subtitle: String
    public var kind: Kind
    public var icon: NodeIcon
    public var target: EditorTarget
    public var tab: ReportTab?
    public var focus: String?
}

public enum FuzzyMatcher {
    /// nil when `query` is not a subsequence of `text`; otherwise higher is better. Rewards a prefix, matches at
    /// word starts and consecutive characters, and penalises gaps and long texts a little.
    public static func score(query: String, in text: String) -> Int? {
        let q = Array(query.lowercased().filter { !$0.isWhitespace })
        if q.isEmpty { return 0 }
        let original = Array(text)
        let t = Array(text.lowercased())
        guard t.count == original.count else { return substringScore(query: q, text: t) }

        var score = 0
        var qi = 0
        var previousMatch = -2
        for ti in 0..<t.count where qi < q.count && t[ti] == q[qi] {
            score += 10
            if ti == 0 { score += 15 }
            if ti > 0 {
                let before = original[ti - 1]
                if !before.isLetter && !before.isNumber { score += 8 }
                else if before.isLowercase && original[ti].isUppercase { score += 8 }
            }
            if ti == previousMatch + 1 { score += 12 }
            else if previousMatch >= 0 { score -= min(6, ti - previousMatch - 1) }
            previousMatch = ti
            qi += 1
        }
        guard qi == q.count else { return nil }
        if String(t).hasPrefix(String(q)) { score += 30 }
        return score - min(10, t.count / 12)
    }

    private static func substringScore(query: [Character], text: [Character]) -> Int? {
        String(text).contains(String(query)) ? 10 * query.count : nil
    }
}

public enum QuickOpen {
    /// Everything that can be opened, from what the app already holds. Jobs are newest first.
    public static func index(state: DashboardState, formatting: DateFormatting, now: Date, catalog: Catalog? = nil) -> [QuickOpenItem] {
        var items: [QuickOpenItem] = []

        for device in state.devices {
            items.append(QuickOpenItem(
                id: "quick:device:\(device.id)", title: device.name, subtitle: "\(device.runtimeDisplayName), \(device.status.label)", kind: .device,
                icon: device.isVM ? .vm : (device.isTablet ? .ipad : .iphone), target: .device(device.id)
            ))
        }
        for run in state.runs {
            items.append(QuickOpenItem(
                id: "quick:run:\(run.id)", title: run.title, subtitle: "Run, \(JobText.runCounts(run))", kind: .run,
                icon: .diamond(JobText.glyph(run.status)), target: .run(run.id)
            ))
        }
        for job in state.jobs {
            let stamp = formatting.dayAndTime(job.finishedOrUpdatedAt, now: now)
            items.append(QuickOpenItem(
                id: "quick:job:\(job.id)", title: JobText.title(job, catalog: catalog), subtitle: "Job, \(stamp)", kind: .job,
                icon: .diamond(JobText.glyph(job.status)), target: .job(job.id), tab: .summary
            ))
            for testCase in state.results[job.id]?.cases ?? [] {
                items.append(QuickOpenItem(
                    id: "quick:test:\(job.id):\(testCase.id)", title: "\(testCase.suiteName).\(testCase.name)", subtitle: "Test in \(job.testTarget), \(stamp)", kind: .test,
                    icon: .diamond(JobText.glyph(testCase.status)), target: .job(job.id), tab: testCase.status == .failed ? .logs : .tests,
                    focus: testCase.message.flatMap { FailureMessage.parse($0).location?.display }
                ))
            }
        }
        return items
    }

    public static func search(_ query: String, in items: [QuickOpenItem], limit: Int = 40) -> [QuickOpenItem] {
        let trimmed = query.trimmingCharacters(in: .whitespaces)
        if trimmed.isEmpty { return Array(items.prefix(limit)) }
        let scored: [(Int, Int, QuickOpenItem)] = items.enumerated().compactMap { offset, item in
            let titleScore = FuzzyMatcher.score(query: trimmed, in: item.title)
            let subtitleScore = FuzzyMatcher.score(query: trimmed, in: item.subtitle).map { $0 / 3 }
            guard let best = [titleScore, subtitleScore].compactMap({ $0 }).max() else { return nil }
            return (best, offset, item)
        }
        return scored.sorted { $0.0 != $1.0 ? $0.0 > $1.0 : $0.1 < $1.1 }.prefix(limit).map(\.2)
    }
}
