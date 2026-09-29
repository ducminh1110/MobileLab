import Foundation

/// Body of `POST /tests/run`.
public struct RunTestRequest: Encodable, Equatable, Sendable {
    public var testTarget: String
    public var projectPath: String?
    public var workspacePath: String?
    public var workingDirectory: String?
    public var configuration: String?
    public var onlyTesting: [String]?
    public var maxRetries: Int?
    public var requiredRuntime: String?
    public var requiredModelId: String?
    public var autoProvision: Bool?

    public init(
        testTarget: String, projectPath: String? = nil, workspacePath: String? = nil, workingDirectory: String? = nil,
        configuration: String? = nil, onlyTesting: [String]? = nil, maxRetries: Int? = nil, requiredRuntime: String? = nil,
        requiredModelId: String? = nil, autoProvision: Bool? = nil
    ) {
        self.testTarget = testTarget
        self.projectPath = projectPath
        self.workspacePath = workspacePath
        self.workingDirectory = workingDirectory
        self.configuration = configuration
        self.onlyTesting = onlyTesting
        self.maxRetries = maxRetries
        self.requiredRuntime = requiredRuntime
        self.requiredModelId = requiredModelId
        self.autoProvision = autoProvision
    }
}

/// Body of `POST /runs`: one job per runtime x device type.
public struct CreateRunRequest: Encodable, Equatable, Sendable {
    public var testTarget: String
    public var name: String?
    public var runtimes: [String]?
    public var models: [String]?
    public var maxParallel: Int?
    public var projectPath: String?
    public var workspacePath: String?
    public var workingDirectory: String?
    public var configuration: String?
    public var onlyTesting: [String]?
    public var maxRetries: Int?
    public var autoProvision: Bool?

    public init(
        testTarget: String, name: String? = nil, runtimes: [String]? = nil, models: [String]? = nil, maxParallel: Int? = nil,
        projectPath: String? = nil, workspacePath: String? = nil, workingDirectory: String? = nil, configuration: String? = nil,
        onlyTesting: [String]? = nil, maxRetries: Int? = nil, autoProvision: Bool? = nil
    ) {
        self.testTarget = testTarget
        self.name = name
        self.runtimes = runtimes
        self.models = models
        self.maxParallel = maxParallel
        self.projectPath = projectPath
        self.workspacePath = workspacePath
        self.workingDirectory = workingDirectory
        self.configuration = configuration
        self.onlyTesting = onlyTesting
        self.maxRetries = maxRetries
        self.autoProvision = autoProvision
    }
}

/// Body of `POST /devices/spawn`.
public struct SpawnDeviceRequest: Encodable, Equatable, Sendable {
    public var name: String?
    public var runtime: String?
    public var modelId: String?
    /// `false` returns 202 at once instead of waiting until the simulator is ready.
    public var wait: Bool?

    public init(name: String? = nil, runtime: String? = nil, modelId: String? = nil, wait: Bool? = nil) {
        self.name = name
        self.runtime = runtime
        self.modelId = modelId
        self.wait = wait
    }
}
