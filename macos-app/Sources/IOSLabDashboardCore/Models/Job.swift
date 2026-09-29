import Foundation

public enum JobStatus: String, Codable, Sendable, CaseIterable {
    case queued
    case running
    case retrying
    case completed
    case failed
    case cancelled
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }

    public var isTerminal: Bool { self == .completed || self == .failed || self == .cancelled }
    /// Still occupies (or waits for) a device.
    public var isActive: Bool { self == .queued || self == .running || self == .retrying }
}

public struct JobTestSummary: Codable, Equatable, Sendable {
    public var total: Int
    public var passed: Int
    public var failed: Int
    public var skipped: Int
    public var durationSeconds: Double
    /// xcodebuild never reached the test phase (compile error, missing scheme, ...).
    public var buildFailed: Bool
    public var errors: [String]

    public init(total: Int = 0, passed: Int = 0, failed: Int = 0, skipped: Int = 0, durationSeconds: Double = 0, buildFailed: Bool = false, errors: [String] = []) {
        self.total = total
        self.passed = passed
        self.failed = failed
        self.skipped = skipped
        self.durationSeconds = durationSeconds
        self.buildFailed = buildFailed
        self.errors = errors
    }

    enum CodingKeys: String, CodingKey { case total, passed, failed, skipped, durationSeconds, buildFailed, errors }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        total = try c.decodeIfPresent(Int.self, forKey: .total) ?? 0
        passed = try c.decodeIfPresent(Int.self, forKey: .passed) ?? 0
        failed = try c.decodeIfPresent(Int.self, forKey: .failed) ?? 0
        skipped = try c.decodeIfPresent(Int.self, forKey: .skipped) ?? 0
        durationSeconds = try c.decodeIfPresent(Double.self, forKey: .durationSeconds) ?? 0
        buildFailed = try c.decodeIfPresent(Bool.self, forKey: .buildFailed) ?? false
        errors = try c.decodeIfPresent([String].self, forKey: .errors) ?? []
    }
}

public struct TestJob: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var runId: String?
    /// The Xcode scheme to test (the API calls it `testTarget` for historical reasons).
    public var testTarget: String
    public var projectPath: String?
    public var workspacePath: String?
    public var workingDirectory: String?
    public var configuration: String?
    public var onlyTesting: [String]?
    public var status: JobStatus
    /// Retries already used.
    public var retries: Int
    public var maxRetries: Int
    /// Number of times execution has started.
    public var attempts: Int
    public var requiredRuntime: String?
    public var requiredModelId: String?
    public var autoProvision: Bool
    public var assignedDeviceId: String?
    public var assignedDeviceName: String?
    /// Why a queued job is not running yet.
    public var waitingReason: String?
    public var exitCode: Int?
    public var error: String?
    public var summary: JobTestSummary?
    public var startedAt: Date?
    public var finishedAt: Date?
    public var durationMs: Double?
    public var createdAt: Date
    public var updatedAt: Date

    public var scheme: String { testTarget }

    public init(
        id: String, runId: String? = nil, testTarget: String, status: JobStatus, retries: Int = 0, maxRetries: Int = 0, attempts: Int = 0,
        autoProvision: Bool = true, assignedDeviceId: String? = nil, assignedDeviceName: String? = nil, waitingReason: String? = nil,
        exitCode: Int? = nil, error: String? = nil, summary: JobTestSummary? = nil, startedAt: Date? = nil, finishedAt: Date? = nil,
        durationMs: Double? = nil, createdAt: Date, updatedAt: Date? = nil, requiredRuntime: String? = nil, requiredModelId: String? = nil
    ) {
        self.id = id
        self.runId = runId
        self.testTarget = testTarget
        self.status = status
        self.retries = retries
        self.maxRetries = maxRetries
        self.attempts = attempts
        self.autoProvision = autoProvision
        self.assignedDeviceId = assignedDeviceId
        self.assignedDeviceName = assignedDeviceName
        self.waitingReason = waitingReason
        self.exitCode = exitCode
        self.error = error
        self.summary = summary
        self.startedAt = startedAt
        self.finishedAt = finishedAt
        self.durationMs = durationMs
        self.createdAt = createdAt
        self.updatedAt = updatedAt ?? createdAt
        self.requiredRuntime = requiredRuntime
        self.requiredModelId = requiredModelId
    }

    enum CodingKeys: String, CodingKey {
        case id, runId, testTarget, projectPath, workspacePath, workingDirectory, configuration, onlyTesting, status
        case retries, maxRetries, attempts, requiredRuntime, requiredModelId, autoProvision
        case assignedDeviceId, assignedDeviceName, waitingReason, exitCode, error, summary
        case startedAt, finishedAt, durationMs, createdAt, updatedAt
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        runId = try c.decodeIfPresent(String.self, forKey: .runId)
        testTarget = try c.decodeIfPresent(String.self, forKey: .testTarget) ?? ""
        projectPath = try c.decodeIfPresent(String.self, forKey: .projectPath)
        workspacePath = try c.decodeIfPresent(String.self, forKey: .workspacePath)
        workingDirectory = try c.decodeIfPresent(String.self, forKey: .workingDirectory)
        configuration = try c.decodeIfPresent(String.self, forKey: .configuration)
        onlyTesting = try c.decodeIfPresent([String].self, forKey: .onlyTesting)
        status = try c.decodeIfPresent(JobStatus.self, forKey: .status) ?? .unknown
        retries = try c.decodeIfPresent(Int.self, forKey: .retries) ?? 0
        maxRetries = try c.decodeIfPresent(Int.self, forKey: .maxRetries) ?? 0
        attempts = try c.decodeIfPresent(Int.self, forKey: .attempts) ?? 0
        requiredRuntime = try c.decodeIfPresent(String.self, forKey: .requiredRuntime)
        requiredModelId = try c.decodeIfPresent(String.self, forKey: .requiredModelId)
        autoProvision = try c.decodeIfPresent(Bool.self, forKey: .autoProvision) ?? true
        assignedDeviceId = try c.decodeIfPresent(String.self, forKey: .assignedDeviceId)
        assignedDeviceName = try c.decodeIfPresent(String.self, forKey: .assignedDeviceName)
        waitingReason = try c.decodeIfPresent(String.self, forKey: .waitingReason)
        exitCode = try c.decodeIfPresent(Int.self, forKey: .exitCode)
        error = try c.decodeIfPresent(String.self, forKey: .error)
        summary = try c.decodeIfPresent(JobTestSummary.self, forKey: .summary)
        startedAt = try c.decodeIfPresent(Date.self, forKey: .startedAt)
        finishedAt = try c.decodeIfPresent(Date.self, forKey: .finishedAt)
        durationMs = try c.decodeIfPresent(Double.self, forKey: .durationMs)
        createdAt = try c.decode(Date.self, forKey: .createdAt)
        updatedAt = try c.decodeIfPresent(Date.self, forKey: .updatedAt) ?? createdAt
    }

    /// How long the job ran, in seconds. `durationMs` is preferred, but the demo runner reports 0 for it,
    /// so a zero falls back to the timestamps. A job that is still running counts up to `now`.
    public func elapsedSeconds(now: Date) -> TimeInterval? {
        guard let startedAt else { return nil }
        if status == .running || status == .retrying, finishedAt == nil { return max(0, now.timeIntervalSince(startedAt)) }
        if let durationMs, durationMs > 0 { return durationMs / 1000 }
        if let finishedAt { return max(0, finishedAt.timeIntervalSince(startedAt)) }
        return nil
    }

    /// The moment to show for a finished job.
    public var finishedOrUpdatedAt: Date { finishedAt ?? updatedAt }

    /// `attempt 2 of 3`: the attempt that runs (or is about to run) out of the most the job may take.
    public func attemptText() -> String {
        let total = maxRetries + 1
        let current = status == .retrying ? attempts + 1 : max(1, attempts)
        return "attempt \(min(current, total)) of \(total)"
    }
}

public enum RunStatus: String, Codable, Sendable {
    case queued
    case running
    case passed
    case failed
    case cancelled
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }

    public var isFinished: Bool { self == .passed || self == .failed || self == .cancelled }
}

public struct JobCounts: Codable, Equatable, Sendable {
    public var queued: Int
    public var running: Int
    public var retrying: Int
    public var completed: Int
    public var failed: Int
    public var cancelled: Int

    public init(queued: Int = 0, running: Int = 0, retrying: Int = 0, completed: Int = 0, failed: Int = 0, cancelled: Int = 0) {
        self.queued = queued
        self.running = running
        self.retrying = retrying
        self.completed = completed
        self.failed = failed
        self.cancelled = cancelled
    }

    enum CodingKeys: String, CodingKey { case queued, running, retrying, completed, failed, cancelled }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        queued = try c.decodeIfPresent(Int.self, forKey: .queued) ?? 0
        running = try c.decodeIfPresent(Int.self, forKey: .running) ?? 0
        retrying = try c.decodeIfPresent(Int.self, forKey: .retrying) ?? 0
        completed = try c.decodeIfPresent(Int.self, forKey: .completed) ?? 0
        failed = try c.decodeIfPresent(Int.self, forKey: .failed) ?? 0
        cancelled = try c.decodeIfPresent(Int.self, forKey: .cancelled) ?? 0
    }

    public var total: Int { queued + running + retrying + completed + failed + cancelled }
}

public struct TestRunView: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var name: String?
    public var scheme: String
    public var jobIds: [String]
    public var maxParallel: Int?
    public var createdAt: Date
    public var status: RunStatus
    public var counts: JobCounts
    public var finishedAt: Date?

    public init(id: String, name: String? = nil, scheme: String, jobIds: [String], maxParallel: Int? = nil, createdAt: Date, status: RunStatus, counts: JobCounts = JobCounts(), finishedAt: Date? = nil) {
        self.id = id
        self.name = name
        self.scheme = scheme
        self.jobIds = jobIds
        self.maxParallel = maxParallel
        self.createdAt = createdAt
        self.status = status
        self.counts = counts
        self.finishedAt = finishedAt
    }

    enum CodingKeys: String, CodingKey { case id, name, scheme, jobIds, maxParallel, createdAt, status, counts, finishedAt }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        name = try c.decodeIfPresent(String.self, forKey: .name)
        scheme = try c.decodeIfPresent(String.self, forKey: .scheme) ?? ""
        jobIds = try c.decodeIfPresent([String].self, forKey: .jobIds) ?? []
        maxParallel = try c.decodeIfPresent(Int.self, forKey: .maxParallel)
        createdAt = try c.decode(Date.self, forKey: .createdAt)
        status = try c.decodeIfPresent(RunStatus.self, forKey: .status) ?? .unknown
        counts = try c.decodeIfPresent(JobCounts.self, forKey: .counts) ?? JobCounts()
        finishedAt = try c.decodeIfPresent(Date.self, forKey: .finishedAt)
    }

    /// The run's own name, or its scheme when it was not named.
    public var title: String {
        if let name, !name.trimmingCharacters(in: .whitespaces).isEmpty { return name }
        return scheme
    }
}

public struct JobsResponse: Codable, Equatable, Sendable {
    public var items: [TestJob]
}

public struct RunsResponse: Codable, Equatable, Sendable {
    public var items: [TestRunView]
}

public struct RunDetail: Codable, Equatable, Sendable {
    public var run: TestRunView
    public var jobs: [TestJob]
}

public struct SkippedCombination: Codable, Equatable, Sendable {
    public var runtime: String
    public var model: String
    public var reason: String
}

public struct CreateRunResponse: Codable, Equatable, Sendable {
    public var run: TestRunView
    public var jobs: [TestJob]
    public var skipped: [SkippedCombination]

    enum CodingKeys: String, CodingKey { case run, jobs, skipped }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        run = try c.decode(TestRunView.self, forKey: .run)
        jobs = try c.decodeIfPresent([TestJob].self, forKey: .jobs) ?? []
        skipped = try c.decodeIfPresent([SkippedCombination].self, forKey: .skipped) ?? []
    }
}

/// `POST /tests/run` and `POST /tests/:id/rerun`.
public struct JobEnvelope: Codable, Equatable, Sendable {
    public var job: TestJob
    public var scheduled: Bool?
}

/// `POST /runs/:id/cancel`.
public struct RunEnvelope: Codable, Equatable, Sendable {
    public var run: TestRunView
}

// MARK: - Results

public enum TestCaseStatus: String, Codable, Sendable {
    case passed
    case failed
    case skipped
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

public struct TestCaseResult: Codable, Equatable, Sendable, Identifiable {
    public var className: String
    public var name: String
    public var status: TestCaseStatus
    public var durationSeconds: Double
    /// Assertion text, usually `File.swift:42 XCTAssertEqual failed: ...`.
    public var message: String?

    public var id: String { className + "/" + name }

    public init(className: String, name: String, status: TestCaseStatus, durationSeconds: Double = 0, message: String? = nil) {
        self.className = className
        self.name = name
        self.status = status
        self.durationSeconds = durationSeconds
        self.message = message
    }

    enum CodingKeys: String, CodingKey { case className, name, status, durationSeconds, message }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        className = try c.decodeIfPresent(String.self, forKey: .className) ?? ""
        name = try c.decode(String.self, forKey: .name)
        status = try c.decodeIfPresent(TestCaseStatus.self, forKey: .status) ?? .unknown
        durationSeconds = try c.decodeIfPresent(Double.self, forKey: .durationSeconds) ?? 0
        message = try c.decodeIfPresent(String.self, forKey: .message)
    }

    /// Class name without the module: `LoginFailTestsTests.LoginTests` becomes `LoginTests`.
    public var suiteName: String {
        className.split(separator: ".").last.map(String.init) ?? className
    }
}

public struct ResultDevice: Codable, Equatable, Sendable {
    public var id: String?
    public var name: String?
    public var runtime: String?
}

/// `GET /tests/:id/results`. A job that has not produced results yet answers `{ attempt: 0, cases: [], summary: null }`.
public struct JobResults: Codable, Equatable, Sendable {
    public var jobId: String?
    public var attempt: Int
    public var device: ResultDevice?
    public var exitCode: Int?
    public var cases: [TestCaseResult]
    public var summary: JobTestSummary?

    public init(jobId: String? = nil, attempt: Int = 0, device: ResultDevice? = nil, exitCode: Int? = nil, cases: [TestCaseResult] = [], summary: JobTestSummary? = nil) {
        self.jobId = jobId
        self.attempt = attempt
        self.device = device
        self.exitCode = exitCode
        self.cases = cases
        self.summary = summary
    }

    enum CodingKeys: String, CodingKey { case jobId, attempt, device, exitCode, cases, summary }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        jobId = try c.decodeIfPresent(String.self, forKey: .jobId)
        attempt = try c.decodeIfPresent(Int.self, forKey: .attempt) ?? 0
        device = try c.decodeIfPresent(ResultDevice.self, forKey: .device)
        exitCode = try c.decodeIfPresent(Int.self, forKey: .exitCode)
        cases = try c.decodeIfPresent([TestCaseResult].self, forKey: .cases) ?? []
        summary = try c.decodeIfPresent(JobTestSummary.self, forKey: .summary)
    }

    public var isEmpty: Bool { cases.isEmpty && summary == nil }
}

// MARK: - Output and artifacts

/// `GET /tests/:id/output`.
public struct JobOutput: Codable, Equatable, Sendable {
    public var text: String
    public var truncated: Bool
    public var sizeBytes: Int
    public var attempt: Int

    public init(text: String, truncated: Bool = false, sizeBytes: Int = 0, attempt: Int = 0) {
        self.text = text
        self.truncated = truncated
        self.sizeBytes = sizeBytes
        self.attempt = attempt
    }

    enum CodingKeys: String, CodingKey { case text, truncated, sizeBytes, attempt }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        text = try c.decodeIfPresent(String.self, forKey: .text) ?? ""
        truncated = try c.decodeIfPresent(Bool.self, forKey: .truncated) ?? false
        sizeBytes = try c.decodeIfPresent(Int.self, forKey: .sizeBytes) ?? 0
        attempt = try c.decodeIfPresent(Int.self, forKey: .attempt) ?? 0
    }
}

public enum ArtifactKind: String, Codable, Sendable {
    case log
    case results
    case xcresult
    case screenshot
    case unknown

    public init(from decoder: Decoder) throws { self = try decodeTolerant(decoder, fallback: .unknown) }
}

public struct Artifact: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var jobId: String
    public var type: ArtifactKind
    public var name: String
    /// Absolute path on the machine that runs the backend.
    public var path: String
    public var isDirectory: Bool
    public var sizeBytes: Int
    public var attempt: Int
    public var createdAt: Date?
    public var downloadUrl: String?
}

public struct ArtifactsResponse: Codable, Equatable, Sendable {
    public var items: [Artifact]
}
