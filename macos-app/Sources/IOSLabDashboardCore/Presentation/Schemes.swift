import Foundation

/// A scheme as the toolbar knows it. The backend has no scheme objects (a job just names one), so schemes
/// are kept on this Mac: the ones a person created plus the names that appear in the job history.
public struct SchemeConfig: Codable, Equatable, Sendable, Identifiable {
    public var id: String
    public var name: String
    public var projectPath: String?
    public var workspacePath: String?
    public var workingDirectory: String?
    public var configuration: String?
    public var onlyTesting: [String]
    public var maxRetries: Int
    public var maxParallel: Int?
    public var autoProvision: Bool

    public init(
        id: String = UUID().uuidString, name: String, projectPath: String? = nil, workspacePath: String? = nil, workingDirectory: String? = nil,
        configuration: String? = nil, onlyTesting: [String] = [], maxRetries: Int = 0, maxParallel: Int? = nil, autoProvision: Bool = true
    ) {
        self.id = id
        self.name = name
        self.projectPath = projectPath
        self.workspacePath = workspacePath
        self.workingDirectory = workingDirectory
        self.configuration = configuration
        self.onlyTesting = onlyTesting
        self.maxRetries = maxRetries
        self.maxParallel = maxParallel
        self.autoProvision = autoProvision
    }

    enum CodingKeys: String, CodingKey {
        case id, name, projectPath, workspacePath, workingDirectory, configuration, onlyTesting, maxRetries, maxParallel, autoProvision
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? UUID().uuidString
        name = try c.decode(String.self, forKey: .name)
        projectPath = try c.decodeIfPresent(String.self, forKey: .projectPath)
        workspacePath = try c.decodeIfPresent(String.self, forKey: .workspacePath)
        workingDirectory = try c.decodeIfPresent(String.self, forKey: .workingDirectory)
        configuration = try c.decodeIfPresent(String.self, forKey: .configuration)
        onlyTesting = try c.decodeIfPresent([String].self, forKey: .onlyTesting) ?? []
        maxRetries = try c.decodeIfPresent(Int.self, forKey: .maxRetries) ?? 0
        maxParallel = try c.decodeIfPresent(Int.self, forKey: .maxParallel)
        autoProvision = try c.decodeIfPresent(Bool.self, forKey: .autoProvision) ?? true
    }

    /// A scheme known only from a past job: no settings, the backend's defaults.
    public static func recent(name: String) -> SchemeConfig { SchemeConfig(id: "recent:\(name)", name: name) }
    public var isRecent: Bool { id.hasPrefix("recent:") }

    /// What is wrong with the values, in the words of the backend's own validation. Empty means valid.
    public func problems() -> [String] {
        var found: [String] = []
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { found.append("Give the scheme a name (the Xcode scheme to test).") }
        if trimmed.hasPrefix("-") { found.append("The name must not start with \"-\".") }
        if trimmed.count > 256 { found.append("The name is too long (256 characters at most).") }
        let hasProject = !(projectPath?.trimmingCharacters(in: .whitespaces).isEmpty ?? true)
        let hasWorkspace = !(workspacePath?.trimmingCharacters(in: .whitespaces).isEmpty ?? true)
        if hasProject && hasWorkspace { found.append("Give either a project or a workspace, not both.") }
        if !(0...5).contains(maxRetries) { found.append("Retries must be between 0 and 5.") }
        if let maxParallel, !(1...32).contains(maxParallel) { found.append("Max parallel must be between 1 and 32.") }
        if onlyTesting.contains(where: { $0.hasPrefix("-") }) { found.append("Test filters must not start with \"-\".") }
        return found
    }

    /// Splits the multi-line text of the "Only testing" field.
    public static func parseFilters(_ text: String) -> [String] {
        text.split(whereSeparator: { $0 == "\n" || $0 == "," }).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }
}

public enum SchemeList {
    /// Created schemes first (alphabetical), then names from the job history, most recently used first.
    public static func merged(stored: [SchemeConfig], jobs: [TestJob]) -> [SchemeConfig] {
        let created = stored.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        let known = Set(created.map(\.name))
        var seen = known
        var recent: [SchemeConfig] = []
        for job in jobs.sorted(by: { $0.createdAt > $1.createdAt }) where !job.testTarget.isEmpty && !seen.contains(job.testTarget) {
            seen.insert(job.testTarget)
            recent.append(.recent(name: job.testTarget))
        }
        return created + recent
    }

    /// The selected scheme, or the most recently used one when nothing valid is selected.
    public static func selected(id: String?, in list: [SchemeConfig], jobs: [TestJob]) -> SchemeConfig? {
        if let id, let match = list.first(where: { $0.id == id }) { return match }
        if let latest = jobs.max(by: { $0.createdAt < $1.createdAt }), let match = list.first(where: { $0.name == latest.testTarget }) { return match }
        return list.first
    }
}

// MARK: - Destinations

/// One destination: a runtime and a device type, either of which may be left to the backend.
public struct DestinationKey: Codable, Hashable, Sendable, Identifiable {
    public var runtime: String?
    public var model: String?

    public init(runtime: String? = nil, model: String? = nil) {
        self.runtime = runtime
        self.model = model
    }

    public var id: String { "\(runtime ?? "*")|\(model ?? "*")" }
    public static let any = DestinationKey()
    public var isAny: Bool { runtime == nil && model == nil }
}

public struct DestinationItem: Equatable, Sendable, Identifiable {
    public var key: DestinationKey
    public var title: String
    public var subtitle: String?
    public var isTablet: Bool

    public var id: String { key.id }
}

public struct DestinationMenu: Equatable, Sendable {
    /// Simulators that are booted right now.
    public var booted: [DestinationItem]
    /// `Create on demand`: every runtime with the device types it can host.
    public var onDemand: [(runtime: CatalogRuntime, items: [DestinationItem])]

    public static func == (lhs: DestinationMenu, rhs: DestinationMenu) -> Bool {
        lhs.booted == rhs.booted && lhs.onDemand.count == rhs.onDemand.count
            && zip(lhs.onDemand, rhs.onDemand).allSatisfy { $0.runtime == $1.runtime && $0.items == $1.items }
    }
}

public enum Destinations {
    public static let anyItem = DestinationItem(key: .any, title: "Any Available Simulator", subtitle: "The backend picks or creates one", isTablet: false)

    public static func menu(devices: [Device], catalog: Catalog?) -> DestinationMenu {
        var seen: Set<DestinationKey> = []
        var booted: [DestinationItem] = []
        for device in devices where device.type == .simulator && device.status.isBooted && device.canRunTests {
            let key = DestinationKey(runtime: device.runtime, model: device.modelId)
            if seen.insert(key).inserted {
                booted.append(DestinationItem(key: key, title: device.name, subtitle: device.runtimeDisplayName, isTablet: device.isTablet))
            }
        }

        var onDemand: [(runtime: CatalogRuntime, items: [DestinationItem])] = []
        if let catalog {
            let runtimes = catalog.runtimes.sorted { RuntimeName.versionComponents($1.name).lexicographicallyPrecedes(RuntimeName.versionComponents($0.name)) }
            for runtime in runtimes {
                let items = catalog.deviceTypes(for: runtime).map {
                    DestinationItem(key: DestinationKey(runtime: runtime.identifier, model: $0.identifier), title: $0.name, subtitle: runtime.name, isTablet: $0.family == "iPad")
                }
                if !items.isEmpty { onDemand.append((runtime, items)) }
            }
        }
        return DestinationMenu(booted: booted, onDemand: onDemand)
    }

    /// What the destination popup shows: `iPhone 15`, `iPhone 15 (iOS 18.0)`, `3 Destinations`, `Any Available Simulator`.
    public static func summary(selected: [DestinationKey], devices: [Device], catalog: Catalog?) -> String {
        let real = selected.filter { !$0.isAny }
        if real.isEmpty { return anyItem.title }
        if real.count > 1 { return "\(real.count) Destinations" }
        let key = real[0]
        return title(for: key, devices: devices, catalog: catalog)
    }

    public static func title(for key: DestinationKey, devices: [Device], catalog: Catalog?) -> String {
        let model = key.model.map { id in devices.first(where: { $0.modelId == id })?.modelName ?? catalog?.deviceType(identifier: id)?.name ?? JobText.modelName(id, catalog: catalog) }
        let runtime = key.runtime.map { id in catalog?.runtime(identifier: id)?.name ?? RuntimeName.display(fromIdentifier: id) }
        switch (model, runtime) {
        case let (model?, runtime?): return "\(model) (\(runtime))"
        case let (model?, nil): return model
        case let (nil, runtime?): return "Any Device (\(runtime))"
        default: return anyItem.title
        }
    }
}

// MARK: - Run planning

public enum RunPlan: Equatable, Sendable {
    case single(RunTestRequest)
    case matrix(CreateRunRequest)
}

public enum RunPlanner {
    /// Turns a scheme and the ticked destinations into backend calls: one destination is a single job, a
    /// full runtime x device-type grid is a matrix run (`POST /runs`), anything else is one job per destination.
    public static func plan(scheme: SchemeConfig, destinations: [DestinationKey]) -> [RunPlan] {
        let unique = uniqueKeys(destinations)
        let name = scheme.name.trimmingCharacters(in: .whitespacesAndNewlines)

        func single(_ key: DestinationKey) -> RunPlan {
            .single(RunTestRequest(
                testTarget: name, projectPath: clean(scheme.projectPath), workspacePath: clean(scheme.workspacePath),
                workingDirectory: clean(scheme.workingDirectory), configuration: clean(scheme.configuration),
                onlyTesting: scheme.onlyTesting.isEmpty ? nil : scheme.onlyTesting, maxRetries: scheme.maxRetries > 0 ? scheme.maxRetries : nil,
                requiredRuntime: key.runtime, requiredModelId: key.model, autoProvision: scheme.autoProvision ? nil : false
            ))
        }

        if unique.isEmpty { return [single(.any)] }
        if unique.count == 1 { return [single(unique[0])] }

        let pinned = unique.filter { $0.runtime != nil && $0.model != nil }
        let loose = unique.filter { $0.runtime == nil || $0.model == nil }
        var plans: [RunPlan] = loose.map(single)

        if !pinned.isEmpty {
            let runtimes = Array(Set(pinned.compactMap(\.runtime))).sorted()
            let models = Array(Set(pinned.compactMap(\.model))).sorted()
            if pinned.count > 1, runtimes.count * models.count == pinned.count {
                plans.append(.matrix(CreateRunRequest(
                    testTarget: name, name: name, runtimes: runtimes, models: models, maxParallel: scheme.maxParallel,
                    projectPath: clean(scheme.projectPath), workspacePath: clean(scheme.workspacePath), workingDirectory: clean(scheme.workingDirectory),
                    configuration: clean(scheme.configuration), onlyTesting: scheme.onlyTesting.isEmpty ? nil : scheme.onlyTesting,
                    maxRetries: scheme.maxRetries > 0 ? scheme.maxRetries : nil, autoProvision: scheme.autoProvision ? nil : false
                )))
            } else {
                plans.append(contentsOf: pinned.map(single))
            }
        }
        return plans
    }

    private static func uniqueKeys(_ keys: [DestinationKey]) -> [DestinationKey] {
        var seen: Set<DestinationKey> = []
        return keys.filter { !$0.isAny || keys.count == 1 }.filter { seen.insert($0).inserted }
    }

    private static func clean(_ text: String?) -> String? {
        guard let trimmed = text?.trimmingCharacters(in: .whitespacesAndNewlines), !trimmed.isEmpty else { return nil }
        return trimmed
    }
}
