import Foundation

/// A gauge row of the Debug navigator: label, value, and a thin usage bar under it when the backend gives
/// something to measure against.
public struct GaugeRow: Equatable, Sendable, Identifiable {
    public var id: String
    public var title: String
    public var symbol: String
    public var value: String
    /// 0...1, or nil when there is no meaningful maximum (no bar is drawn).
    public var fraction: Double?
}

public struct DebugModel: Equatable, Sendable {
    public var title: String
    public var pid: Int?
    public var gauges: [GaugeRow]
    /// Running jobs are threads, queued jobs are greyed threads.
    public var threads: [TreeNode]
    /// False until the first `/metrics/summary` answer.
    public var hasMetrics: Bool
}

public enum DebugNavigator {
    public static func build(metrics: MetricsSummary?, capacity: CapacitySnapshot?, jobs: [TestJob], catalog: Catalog? = nil) -> DebugModel {
        var gauges: [GaugeRow] = []

        if let process = metrics?.process {
            let cores = Double(max(1, capacity?.cpuCores ?? metrics?.capacity?.cpuCores ?? 1))
            gauges.append(GaugeRow(
                id: "cpu", title: "CPU", symbol: "cpu", value: Formatters.percent(process.cpuPercent),
                fraction: clamp(process.cpuPercent / (100 * cores))
            ))

            var memoryFraction: Double?
            if let memoryGb = capacity?.memoryGb ?? metrics?.capacity?.memoryGb, memoryGb > 0 {
                memoryFraction = clamp(Double(process.rssBytes) / (memoryGb * 1024 * 1024 * 1024))
            }
            gauges.append(GaugeRow(id: "memory", title: "Memory", symbol: "memorychip", value: Formatters.bytes(process.rssBytes), fraction: memoryFraction))
        }

        if let metrics {
            gauges.append(GaugeRow(id: "disk", title: "Disk (artifacts)", symbol: "internaldrive", value: Formatters.bytes(metrics.artifactBytes), fraction: nil))
        }

        if let capacity = capacity ?? metrics?.capacity {
            gauges.append(GaugeRow(
                id: "capacity", title: "Capacity", symbol: "square.stack.3d.up", value: "\(capacity.load) of \(capacity.maxLoad)",
                fraction: capacity.fraction
            ))
        }

        return DebugModel(
            title: "MobileLab Backend", pid: metrics?.process?.pid, gauges: gauges,
            threads: threads(jobs: jobs, catalog: catalog), hasMetrics: metrics != nil
        )
    }

    static func threads(jobs: [TestJob], catalog: Catalog?) -> [TreeNode] {
        let running = jobs.filter { $0.status == .running || $0.status == .retrying }.sorted { ($0.startedAt ?? $0.createdAt) < ($1.startedAt ?? $1.createdAt) }
        let queued = jobs.filter { $0.status == .queued }.sorted { $0.createdAt < $1.createdAt }

        var nodes: [TreeNode] = []
        for (index, job) in (running + queued).enumerated() {
            let isQueued = job.status == .queued
            var frames: [TreeNode] = []
            if isQueued {
                frames.append(frame(job, 0, job.waitingReason ?? "Waiting to start"))
            } else {
                frames.append(frame(job, 0, JobText.title(job, catalog: catalog)))
                frames.append(frame(job, 1, job.attemptText()))
                frames.append(frame(job, 2, "job \(Formatters.shortID(job.id))"))
            }
            nodes.append(TreeNode(
                id: "thread:\(job.id)", title: "Thread \(index + 1)", subtitle: "Queue: \(job.testTarget)", icon: .thread,
                children: frames, target: .job(job.id), isDimmed: isQueued
            ))
        }
        return nodes
    }

    private static func frame(_ job: TestJob, _ index: Int, _ text: String) -> TreeNode {
        TreeNode(id: "frame:\(job.id):\(index)", title: "\(index) \(text)", icon: .frame, target: .job(job.id), isDimmed: job.status == .queued)
    }

    private static func clamp(_ value: Double) -> Double { min(1, max(0, value)) }
}

// MARK: - Variables view

public enum VariablesMode: String, CaseIterable, Codable, Sendable {
    case all
    case failures
    case running

    public var title: String {
        switch self {
        case .all: return "All"
        case .failures: return "Failures"
        case .running: return "Running"
        }
    }
}

/// The debug area's left half: the open job's test cases as a tree of values, like Xcode's variables view.
public enum VariablesTree {
    public static func build(job: TestJob?, results: JobResults?, runningTest: String?, mode: VariablesMode, query: String = "") -> [TreeNode] {
        guard let job else { return [] }
        let needle = query.trimmingCharacters(in: .whitespaces)

        var rows: [TreeNode] = []

        if mode == .running {
            if let runningTest {
                rows.append(TreeNode(id: "var:running", title: runningTest, icon: .none, badge: NodeBadge("R", .neutral), value: "= running"))
            }
            return rows
        }

        guard let results, !results.cases.isEmpty else {
            return jobProperties(job, needle: needle)
        }

        var order: [String] = []
        var grouped: [String: [TestCaseResult]] = [:]
        for testCase in results.cases {
            if mode == .failures, testCase.status != .failed { continue }
            if !needle.isEmpty, testCase.name.range(of: needle, options: .caseInsensitive) == nil, testCase.className.range(of: needle, options: .caseInsensitive) == nil { continue }
            if grouped[testCase.className] == nil { order.append(testCase.className) }
            grouped[testCase.className, default: []].append(testCase)
        }

        for className in order {
            let members = grouped[className] ?? []
            let failed = members.filter { $0.status == .failed }.count
            let value = failed > 0 ? "= \(failed) of \(members.count) failed" : "= \(Formatters.plural(members.count, "test"))"
            let children = members.map { testCase -> TreeNode in
                var detail: [TreeNode] = []
                if let message = testCase.message, !message.isEmpty {
                    detail.append(TreeNode(id: "var:\(job.id):\(testCase.id):message", title: "message", value: "= \"\(message.replacingOccurrences(of: "\n", with: " "))\""))
                }
                return TreeNode(
                    id: "var:\(job.id):\(testCase.id)", title: testCase.name, badge: badge(testCase.status),
                    value: "= \(testCase.status.rawValue): \(Formatters.duration(testCase.durationSeconds))", children: detail,
                    target: .job(job.id), tab: testCase.status == .failed ? .logs : .tests
                )
            }
            rows.append(TreeNode(
                id: "var:\(job.id):\(className)", title: members.first?.suiteName ?? className, badge: NodeBadge("C", .neutral),
                value: value, children: children, target: .job(job.id)
            ))
        }
        return rows
    }

    /// While a job has no parsed results (queued, running) its own fields are what there is to inspect.
    static func jobProperties(_ job: TestJob, needle: String) -> [TreeNode] {
        var rows: [(String, String)] = [("scheme", job.testTarget), ("status", job.status.rawValue), ("attempt", job.attemptText())]
        if let device = job.assignedDeviceName { rows.append(("device", device)) }
        if let reason = job.waitingReason { rows.append(("waitingReason", reason)) }
        if let error = job.error { rows.append(("error", error)) }
        return rows
            .filter { needle.isEmpty || $0.0.range(of: needle, options: .caseInsensitive) != nil || $0.1.range(of: needle, options: .caseInsensitive) != nil }
            .map { TreeNode(id: "var:\(job.id):\($0.0)", title: $0.0, value: "= \"\($0.1)\"") }
    }

    static func badge(_ status: TestCaseStatus) -> NodeBadge {
        switch status {
        case .passed: return NodeBadge("P", .pass)
        case .failed: return NodeBadge("F", .fail)
        case .skipped: return NodeBadge("S", .skip)
        case .unknown: return NodeBadge("?", .neutral)
        }
    }
}
