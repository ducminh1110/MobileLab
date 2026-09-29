import Foundation

public enum StatusTone: Equatable, Sendable {
    case neutral
    case success
    case failure
    case running
    case warning
    case disconnected
}

public enum StatusGlyph: String, Equatable, Sendable {
    case ready
    case queued
    case running
    case retrying
    case passed
    case failed
    case cancelled
    case disconnected
    case connecting

    /// SF Symbol for the capsule; status is always glyph and colour, never colour alone.
    public var symbolName: String {
        switch self {
        case .ready: return "circle"
        case .queued: return "clock"
        case .running: return "circle.dotted"
        case .retrying: return "arrow.clockwise"
        case .passed: return "checkmark.diamond.fill"
        case .failed: return "xmark.diamond.fill"
        case .cancelled: return "minus.circle"
        case .disconnected: return "exclamationmark.triangle.fill"
        case .connecting: return "ellipsis.circle"
        }
    }
}

/// The text of the toolbar capsule: `**State** | detail`. Built only from real jobs and devices.
public struct CapsuleStatus: Equatable, Sendable {
    public var state: String
    public var detail: String?
    public var tone: StatusTone
    public var glyph: StatusGlyph
    public var showsSpinner: Bool
    /// The small `Demo` tag before the state.
    public var isDemo: Bool
    /// The job the status describes; clicking the status opens its report.
    public var jobID: String?

    public init(state: String, detail: String? = nil, tone: StatusTone = .neutral, glyph: StatusGlyph = .ready, showsSpinner: Bool = false, isDemo: Bool = false, jobID: String? = nil) {
        self.state = state
        self.detail = detail
        self.tone = tone
        self.glyph = glyph
        self.showsSpinner = showsSpinner
        self.isDemo = isDemo
        self.jobID = jobID
    }

    /// `Tests Failed | 2 of 8 tests failed`, for tooltips and accessibility.
    public var text: String {
        guard let detail, !detail.isEmpty else { return state }
        return "\(state) | \(detail)"
    }

    /// Which job the capsule describes: a running one (of this scheme first), else a queued one, else the most
    /// recent job of the scheme. A scheme that has never run has no job, whatever other schemes did.
    public static func pickJob(scheme: String?, jobs: [TestJob]) -> TestJob? {
        let same = jobs.filter { scheme == nil || $0.testTarget == scheme }

        func newestRunning(_ list: [TestJob]) -> TestJob? {
            list.filter { $0.status == .running || $0.status == .retrying }
                .max { ($0.startedAt ?? $0.createdAt) < ($1.startedAt ?? $1.createdAt) }
        }
        func newestQueued(_ list: [TestJob]) -> TestJob? {
            list.filter { $0.status == .queued }.max { $0.createdAt < $1.createdAt }
        }

        if let running = newestRunning(same) ?? newestRunning(jobs) { return running }
        if let queued = newestQueued(same) ?? newestQueued(jobs) { return queued }
        return same.max { $0.createdAt < $1.createdAt }
    }

    public static func make(
        scheme: String?, state: DashboardState, now: Date, formatting: DateFormatting
    ) -> CapsuleStatus {
        let demo = state.isDemo

        switch state.connection {
        case .connecting where !state.hasLoaded:
            return CapsuleStatus(state: "Connecting", detail: "Reaching the backend", tone: .neutral, glyph: .connecting, isDemo: demo)
        case .disconnected:
            return CapsuleStatus(state: "Disconnected", detail: "Retrying\u{2026}", tone: .disconnected, glyph: .disconnected, isDemo: demo)
        default:
            break
        }

        guard let job = pickJob(scheme: scheme, jobs: state.jobs) else {
            let count = state.bootedSimulatorCount
            let hasSimulators = state.devices.contains { $0.type == .simulator }
            let detail = hasSimulators ? "\(Formatters.plural(count, "simulator")) booted" : "No simulators"
            return CapsuleStatus(state: "Ready", detail: detail, tone: .neutral, glyph: .ready, isDemo: demo)
        }

        switch job.status {
        case .queued:
            return CapsuleStatus(state: "Queued", detail: job.waitingReason ?? "Waiting to start", tone: .neutral, glyph: .queued, isDemo: demo, jobID: job.id)

        case .running:
            var detail = job.testTarget
            if let device = job.assignedDeviceName, !device.isEmpty { detail += " on \(device)" }
            if let seconds = job.elapsedSeconds(now: now) { detail += ", \(Formatters.elapsed(seconds))" }
            return CapsuleStatus(state: "Running", detail: detail, tone: .running, glyph: .running, showsSpinner: true, isDemo: demo, jobID: job.id)

        case .retrying:
            return CapsuleStatus(state: "Retrying", detail: job.attemptText(), tone: .warning, glyph: .retrying, isDemo: demo, jobID: job.id)

        case .completed:
            if let summary = job.summary, summary.total == 0, !summary.buildFailed {
                return CapsuleStatus(state: "Completed", detail: "No tests were run", tone: .warning, glyph: .passed, isDemo: demo, jobID: job.id)
            }
            return CapsuleStatus(
                state: "Tests Passed", detail: formatting.dayAndTime(job.finishedOrUpdatedAt, now: now),
                tone: .success, glyph: .passed, isDemo: demo, jobID: job.id
            )

        case .failed:
            if let summary = job.summary, summary.buildFailed {
                let detail = summary.errors.first ?? job.error ?? "xcodebuild did not reach the tests"
                return CapsuleStatus(state: "Build Failed", detail: detail, tone: .failure, glyph: .failed, isDemo: demo, jobID: job.id)
            }
            if let summary = job.summary, summary.failed > 0 {
                return CapsuleStatus(
                    state: "Tests Failed", detail: "\(summary.failed) of \(summary.total) tests failed",
                    tone: .failure, glyph: .failed, isDemo: demo, jobID: job.id
                )
            }
            return CapsuleStatus(state: "Failed", detail: job.error ?? exitDetail(job), tone: .failure, glyph: .failed, isDemo: demo, jobID: job.id)

        case .cancelled:
            return CapsuleStatus(
                state: "Cancelled", detail: formatting.dayAndTime(job.finishedOrUpdatedAt, now: now),
                tone: .neutral, glyph: .cancelled, isDemo: demo, jobID: job.id
            )

        case .unknown:
            return CapsuleStatus(state: "Ready", detail: nil, tone: .neutral, glyph: .ready, isDemo: demo, jobID: job.id)
        }
    }

    private static func exitDetail(_ job: TestJob) -> String {
        if let code = job.exitCode { return "xcodebuild exited with code \(code)" }
        return "The job did not finish"
    }
}
