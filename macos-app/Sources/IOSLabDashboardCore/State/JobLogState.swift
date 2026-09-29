import Foundation

/// The output of the job that is open in the editor: a fetched snapshot plus the lines streamed after it.
///
/// Order of events when a job is opened: the live stream (`output=1`) is subscribed first, lines that arrive
/// before the snapshot comes back are buffered, then `applySnapshot` merges them without duplicates.
public struct JobLogState: Equatable, Sendable {
    public private(set) var jobID: String?
    public private(set) var document = LogDocument()
    public private(set) var isLoading = false
    public private(set) var error: String?
    private var pendingLive: [String] = []
    private var snapshotApplied = false

    public init() {}

    public var isEmpty: Bool { document.isEmpty }

    /// Point the log at another job (or at nothing).
    public mutating func reset(jobID: String?) {
        self = JobLogState()
        self.jobID = jobID
        self.isLoading = jobID != nil
    }

    public mutating func applySnapshot(_ output: JobOutput) {
        document = LogDocument.merging(snapshot: output, live: pendingLive)
        pendingLive = []
        snapshotApplied = true
        isLoading = false
        error = nil
    }

    /// Replace everything with the final text once the job is over, so no streamed line can be missing or doubled.
    public mutating func replaceWithFinal(_ output: JobOutput) {
        document = LogDocument(text: output.text, truncated: output.truncated)
        pendingLive = []
        snapshotApplied = true
        isLoading = false
        error = nil
    }

    public mutating func failed(_ message: String) {
        isLoading = false
        error = message
    }

    /// One streamed output event. Events of other jobs are ignored.
    public mutating func appendLive(_ event: EngineEvent) {
        guard event.isOutput, event.jobId == jobID else { return }
        let lines = LogDocument.splitLines(event.message)
        if snapshotApplied {
            document.appendLines(lines)
        } else {
            pendingLive.append(contentsOf: lines)
        }
    }

    /// The test that has started and not finished yet, for the debug bar (`Thread 1 > testInvalidPassword`).
    public var runningTestName: String? {
        var finished: Set<String> = []
        for line in document.lines.reversed() {
            guard let name = TestCaseName.parse(line.text) else { continue }
            switch line.info.kind {
            case .casePassed, .caseFailed, .caseSkipped: finished.insert(name.qualified)
            case .caseStarted: if !finished.contains(name.qualified) { return name.method }
            default: continue
            }
        }
        return nil
    }
}

/// The class and method inside `Test Case '-[Module.Class method]' started.` (or the `Class.method()` spelling).
public struct TestCaseName: Equatable, Sendable {
    public var className: String
    public var method: String

    public var qualified: String { "\(className)/\(method)" }

    public static func parse(_ line: String) -> TestCaseName? {
        let trimmed = line.drop(while: { $0 == " " || $0 == "\t" })
        guard trimmed.hasPrefix("Test Case '") || trimmed.hasPrefix("Test case '") else { return nil }
        guard let open = trimmed.firstIndex(of: "'") else { return nil }
        let afterOpen = trimmed.index(after: open)
        guard let close = trimmed[afterOpen...].firstIndex(of: "'") else { return nil }
        let inner = String(trimmed[afterOpen..<close])

        if inner.hasPrefix("-["), inner.hasSuffix("]"), let space = inner.firstIndex(of: " ") {
            let className = inner[inner.index(inner.startIndex, offsetBy: 2)..<space]
            let method = inner[inner.index(after: space)..<inner.index(before: inner.endIndex)]
            return TestCaseName(className: String(className), method: String(method))
        }
        var name = inner
        if name.hasSuffix("()") { name.removeLast(2) }
        if let dot = name.lastIndex(of: ".") {
            return TestCaseName(className: String(name[..<dot]), method: String(name[name.index(after: dot)...]))
        }
        return TestCaseName(className: "", method: name)
    }
}
