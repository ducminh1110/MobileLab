import Foundation

// MARK: - Shared wording

public enum JobText {
    /// `iPhone 15 (iOS 18.0)` for what the job runs on: the assigned device, else what it asked for.
    public static func destination(_ job: TestJob, catalog: Catalog? = nil) -> String? {
        if let name = job.assignedDeviceName, !name.isEmpty { return name }
        let runtime = job.requiredRuntime.map { runtimeName($0, catalog: catalog) }
        let model = job.requiredModelId.map { modelName($0, catalog: catalog) }
        switch (model, runtime) {
        case let (model?, runtime?): return "\(model) (\(runtime))"
        case let (model?, nil): return model
        case let (nil, runtime?): return runtime
        default: return nil
        }
    }

    /// `DemoApp on iPhone 15 Test`, or just the scheme when no device is known yet.
    public static func title(_ job: TestJob, catalog: Catalog? = nil) -> String {
        if let destination = destination(job, catalog: catalog) { return "\(job.testTarget) on \(destination)" }
        return job.testTarget
    }

    static func runtimeName(_ identifier: String, catalog: Catalog?) -> String {
        catalog?.runtime(identifier: identifier)?.name ?? RuntimeName.display(fromIdentifier: identifier)
    }

    static func modelName(_ identifier: String, catalog: Catalog?) -> String {
        if let name = catalog?.deviceType(identifier: identifier)?.name { return name }
        let marker = "SimDeviceType."
        if let range = identifier.range(of: marker) { return identifier[range.upperBound...].replacingOccurrences(of: "-", with: " ") }
        return identifier
    }

    public static func glyph(_ status: JobStatus) -> TestGlyph {
        switch status {
        case .completed: return .passed
        case .failed: return .failed
        case .running, .retrying: return .running
        case .queued, .unknown: return .notRun
        case .cancelled: return .cancelled
        }
    }

    public static func glyph(_ status: RunStatus) -> TestGlyph {
        switch status {
        case .passed: return .passed
        case .failed: return .failed
        case .running: return .running
        case .queued, .unknown: return .notRun
        case .cancelled: return .cancelled
        }
    }

    public static func glyph(_ status: TestCaseStatus) -> TestGlyph {
        switch status {
        case .passed: return .passed
        case .failed: return .failed
        case .skipped: return .skipped
        case .unknown: return .notRun
        }
    }

    /// `2 of 8 tests failed`, `8 tests passed`, `No tests were run`.
    public static func resultLine(_ summary: JobTestSummary) -> String {
        if summary.buildFailed { return "Build failed" }
        if summary.total == 0 { return "No tests were run" }
        if summary.failed > 0 { return "\(summary.failed) of \(summary.total) tests failed" }
        return "\(Formatters.plural(summary.total, "test")) passed"
    }

    /// Right-hand text of a run row: `3 passed, 1 failed` or `1 running, 3 queued`.
    public static func runCounts(_ run: TestRunView) -> String {
        let c = run.counts
        var parts: [String] = []
        if c.running + c.retrying > 0 { parts.append("\(c.running + c.retrying) running") }
        if c.queued > 0 { parts.append("\(c.queued) queued") }
        if c.completed > 0 { parts.append("\(c.completed) passed") }
        if c.failed > 0 { parts.append("\(c.failed) failed") }
        if c.cancelled > 0 { parts.append("\(c.cancelled) cancelled") }
        return parts.isEmpty ? Formatters.plural(run.jobIds.count, "job") : parts.joined(separator: ", ")
    }
}

// MARK: - Devices

public enum DevicesTree {
    public static let simulatorsRootID = "devices.simulators"
    public static let vmRootID = "devices.vms"

    public static func runtimeFolderID(_ runtime: String) -> String { "runtime:\(runtime)" }
    public static func deviceNodeID(_ id: String) -> String { "device:\(id)" }

    /// `Simulators` > a blue folder per runtime (newest first) > devices; a second root for the simulated VMs.
    public static func build(devices: [Device], vmEnabled: Bool, filter: NavigatorFilter = NavigatorFilter()) -> [TreeNode] {
        let visible = devices.filter { filter.onlyProblems ? $0.status.isBooted : true }
        let simulators = visible.filter { $0.type != .vm }
        let vms = visible.filter { $0.type == .vm }

        var roots: [TreeNode] = []

        var byRuntime: [String: [Device]] = [:]
        var order: [String] = []
        for device in simulators {
            if byRuntime[device.runtime] == nil { order.append(device.runtime) }
            byRuntime[device.runtime, default: []].append(device)
        }
        let folders: [TreeNode] = order
            .sorted { versionLess(byRuntime[$1]?.first, byRuntime[$0]?.first) }
            .map { runtime in
                let group = byRuntime[runtime] ?? []
                let title = group.first?.runtimeDisplayName ?? RuntimeName.display(fromIdentifier: runtime)
                return TreeNode(
                    id: runtimeFolderID(runtime), title: title, icon: .folder,
                    children: group.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }.map(node(for:))
                )
            }
        roots.append(TreeNode(id: simulatorsRootID, title: "Simulators", icon: .simulators, children: folders))

        if vmEnabled || !vms.isEmpty {
            roots.append(TreeNode(
                id: vmRootID, title: "Experimental VMs (simulated)", icon: .folder,
                children: vms.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }.map(node(for:))
            ))
        }
        return TreeFilter.apply(roots, filter: filter)
    }

    static func node(for device: Device) -> TreeNode {
        TreeNode(
            id: deviceNodeID(device.id), title: device.name,
            icon: device.isVM ? .vm : (device.isTablet ? .ipad : .iphone),
            dot: StatusDot(device.status), tags: device.ephemeral ? ["auto"] : [],
            target: .device(device.id)
        )
    }

    /// Newer runtimes first.
    private static func versionLess(_ a: Device?, _ b: Device?) -> Bool {
        let left = RuntimeName.versionComponents(a?.runtimeDisplayName ?? "")
        let right = RuntimeName.versionComponents(b?.runtimeDisplayName ?? "")
        return left.lexicographicallyPrecedes(right)
    }
}

// MARK: - Tests

public enum TestsTree {
    public static let rootID = "tests.runs"

    public static func runNodeID(_ id: String) -> String { "run:\(id)" }
    public static func jobNodeID(_ id: String) -> String { "job:\(id)" }
    public static func suiteNodeID(job: String, className: String) -> String { "suite:\(job):\(className)" }
    public static func caseNodeID(job: String, caseID: String) -> String { "case:\(job):\(caseID)" }

    /// `Runs` > run (matrix) > job > suite (class) > test case. Jobs that belong to no run sit directly under `Runs`.
    public static func build(
        runs: [TestRunView], jobs: [TestJob], results: [String: JobResults], catalog: Catalog? = nil,
        filter: NavigatorFilter = NavigatorFilter()
    ) -> [TreeNode] {
        let jobsByID = Dictionary(jobs.map { ($0.id, $0) }, uniquingKeysWith: { first, _ in first })
        var inRun: Set<String> = []
        var entries: [(date: Date, node: TreeNode)] = []

        for run in runs {
            let members = run.jobIds.compactMap { jobsByID[$0] }
            members.forEach { inRun.insert($0.id) }
            if filter.onlyProblems, run.status != .failed, !members.contains(where: { $0.status == .failed }) { continue }
            let children = members
                .filter { !filter.onlyProblems || $0.status == .failed }
                .map { jobNode($0, results: results[$0.id], catalog: catalog, filter: filter) }
            entries.append((run.createdAt, TreeNode(
                id: runNodeID(run.id), title: run.title, subtitle: run.name == nil ? nil : run.scheme,
                icon: .diamond(JobText.glyph(run.status)), trailing: JobText.runCounts(run),
                children: children, target: .run(run.id)
            )))
        }

        for job in jobs where !inRun.contains(job.id) {
            if filter.onlyProblems, job.status != .failed { continue }
            entries.append((job.createdAt, jobNode(job, results: results[job.id], catalog: catalog, filter: filter)))
        }

        let children = entries.sorted { $0.date > $1.date }.map(\.node)
        let root = TreeNode(id: rootID, title: "Runs", icon: .runs, children: children)
        return TreeFilter.apply([root], filter: filter)
    }

    static func jobNode(_ job: TestJob, results: JobResults?, catalog: Catalog?, filter: NavigatorFilter) -> TreeNode {
        var children: [TreeNode] = []
        var lazy: String?

        if let results {
            children = suiteNodes(job: job, cases: results.cases, onlyFailed: filter.onlyProblems)
        } else if job.status.isTerminal || job.attempts > 0 {
            lazy = job.id
        }

        var trailing: String?
        if job.status.isTerminal, let seconds = job.elapsedSeconds(now: job.finishedOrUpdatedAt) { trailing = Formatters.duration(seconds) }
        if job.status == .queued { trailing = "queued" }

        return TreeNode(
            id: jobNodeID(job.id), title: JobText.title(job, catalog: catalog), icon: .diamond(JobText.glyph(job.status)),
            trailing: trailing, children: children, lazyJobID: children.isEmpty ? lazy : nil,
            target: .job(job.id), tab: job.status == .failed ? .summary : nil
        )
    }

    static func suiteNodes(job: TestJob, cases: [TestCaseResult], onlyFailed: Bool) -> [TreeNode] {
        var order: [String] = []
        var grouped: [String: [TestCaseResult]] = [:]
        for testCase in cases {
            if grouped[testCase.className] == nil { order.append(testCase.className) }
            grouped[testCase.className, default: []].append(testCase)
        }
        return order.compactMap { className in
            let members = (grouped[className] ?? []).filter { !onlyFailed || $0.status == .failed }
            if members.isEmpty { return nil }
            let glyph: TestGlyph = members.contains { $0.status == .failed } ? .failed : (members.allSatisfy { $0.status == .skipped } ? .skipped : .passed)
            return TreeNode(
                id: suiteNodeID(job: job.id, className: className),
                title: members.first?.suiteName ?? className, icon: .diamond(glyph),
                children: members.map { caseNode($0, job: job) }, target: .job(job.id), tab: .tests
            )
        }
    }

    static func caseNode(_ testCase: TestCaseResult, job: TestJob) -> TreeNode {
        var focus: String?
        var tab: ReportTab = .tests
        if testCase.status == .failed {
            tab = .logs
            focus = testCase.message.flatMap { FailureMessage.parse($0).location?.display }
        }
        return TreeNode(
            id: caseNodeID(job: job.id, caseID: testCase.id), title: testCase.name, icon: .diamond(JobText.glyph(testCase.status)),
            trailing: Formatters.duration(testCase.durationSeconds), target: .job(job.id), tab: tab, focus: focus
        )
    }
}

// MARK: - Issues

public enum IssuesTree {
    public static func groupID(_ jobID: String) -> String { "issues:\(jobID)" }

    /// Failed jobs, newest first, each with its failed tests, build errors or job error.
    public static func build(
        jobs: [TestJob], results: [String: JobResults], now: Date, formatting: DateFormatting, catalog: Catalog? = nil,
        filter: NavigatorFilter = NavigatorFilter()
    ) -> [TreeNode] {
        let failed = jobs.filter { $0.status == .failed }.sorted { $0.finishedOrUpdatedAt > $1.finishedOrUpdatedAt }
        let groups: [TreeNode] = failed.compactMap { job in
            let issues = issueNodes(for: job, results: results[job.id])
            if issues.isEmpty { return nil }
            return TreeNode(
                id: groupID(job.id), title: JobText.title(job, catalog: catalog), icon: .diamond(.failed),
                trailing: formatting.dayAndTime(job.finishedOrUpdatedAt, now: now), children: issues,
                target: .job(job.id), tab: .summary
            )
        }
        return TreeFilter.apply(groups, filter: filter)
    }

    /// Number of issues across failed jobs, for the tab badge.
    public static func count(jobs: [TestJob], results: [String: JobResults]) -> Int {
        jobs.filter { $0.status == .failed }.reduce(0) { $0 + issueNodes(for: $1, results: results[$1.id]).count }
    }

    static func issueNodes(for job: TestJob, results: JobResults?) -> [TreeNode] {
        var nodes: [TreeNode] = []
        let summary = results?.summary ?? job.summary

        if let summary, summary.buildFailed {
            for (index, error) in summary.errors.enumerated() {
                let parsed = FailureMessage.parse(error)
                nodes.append(TreeNode(
                    id: "issue:\(job.id):build:\(index)", title: parsed.text, icon: .issue(.buildError),
                    trailing: parsed.location?.display, target: .job(job.id), tab: .logs,
                    focus: parsed.location?.display ?? String(error.prefix(60))
                ))
            }
            if nodes.isEmpty {
                nodes.append(TreeNode(
                    id: "issue:\(job.id):build", title: job.error ?? "The build failed", icon: .issue(.buildError),
                    target: .job(job.id), tab: .logs
                ))
            }
            return nodes
        }

        if let results, !results.cases.isEmpty {
            for testCase in results.cases where testCase.status == .failed {
                let parsed = testCase.message.map(FailureMessage.parse)
                nodes.append(TreeNode(
                    id: "issue:\(job.id):\(testCase.id)", title: parsed?.text.isEmpty == false ? parsed!.text : "\(testCase.name) failed",
                    subtitle: "\(testCase.suiteName).\(testCase.name)", icon: .issue(.testFailure),
                    trailing: parsed?.location?.display, target: .job(job.id), tab: .logs, focus: parsed?.location?.display
                ))
            }
        } else if let summary, summary.failed > 0 {
            // Results are still being fetched: show what the job itself reports.
            nodes.append(TreeNode(
                id: "issue:\(job.id):summary", title: job.error ?? JobText.resultLine(summary), icon: .issue(.testFailure),
                target: .job(job.id), tab: .summary
            ))
        }

        if nodes.isEmpty, let error = job.error, !error.isEmpty {
            nodes.append(TreeNode(id: "issue:\(job.id):error", title: error, icon: .issue(.runtimeError), target: .job(job.id), tab: .summary))
        }
        return nodes
    }
}

// MARK: - Reports

public enum ReportsTree {
    /// Finished jobs, newest first, under `Today` / `Yesterday` / date headers.
    public static func build(
        jobs: [TestJob], now: Date, formatting: DateFormatting, catalog: Catalog? = nil, filter: NavigatorFilter = NavigatorFilter()
    ) -> [TreeNode] {
        let finished = jobs
            .filter { $0.status.isTerminal && (!filter.onlyProblems || $0.status == .failed) }
            .sorted { $0.finishedOrUpdatedAt > $1.finishedOrUpdatedAt }

        var headers: [String] = []
        var grouped: [String: [TreeNode]] = [:]
        for job in finished {
            let heading = formatting.day(job.finishedOrUpdatedAt, now: now)
            if grouped[heading] == nil { headers.append(heading) }
            var trailing = formatting.time(job.finishedOrUpdatedAt)
            if let seconds = job.elapsedSeconds(now: now) { trailing += "  " + Formatters.duration(seconds) }
            grouped[heading, default: []].append(TreeNode(
                id: "report:\(job.id)", title: JobText.title(job, catalog: catalog), icon: .diamond(JobText.glyph(job.status)),
                trailing: trailing, target: .job(job.id), tab: .summary
            ))
        }
        let sections = headers.map { heading in
            TreeNode(id: "reports:\(heading)", title: heading, icon: .none, children: grouped[heading] ?? [], isHeader: true)
        }
        return TreeFilter.apply(sections, filter: filter)
    }
}
