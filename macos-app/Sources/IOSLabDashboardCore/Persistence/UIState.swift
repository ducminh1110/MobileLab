import Foundation

public enum AppearanceChoice: String, Codable, CaseIterable, Sendable, Identifiable {
    case system
    case light
    case dark

    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .system: return "System"
        case .light: return "Light"
        case .dark: return "Dark"
        }
    }
}

/// Which halves of the debug area are shown.
public enum DebugLayout: String, Codable, CaseIterable, Sendable {
    case both
    case variablesOnly
    case consoleOnly
}

/// Sizes of the panels, with the limits from the design spec.
public enum PanelLimits {
    public static let navigator: ClosedRange<Double> = 220...460
    public static let inspector: ClosedRange<Double> = 240...460
    public static let debugMinimum: Double = 120
    public static let navigatorDefault: Double = 300
    public static let inspectorDefault: Double = 300
    public static let debugDefault: Double = 240
    public static let minimumWindow = (width: 1000.0, height: 640.0)

    public static func clamp(_ value: Double, to range: ClosedRange<Double>) -> Double {
        guard value.isFinite else { return range.lowerBound }
        return min(range.upperBound, max(range.lowerBound, value))
    }
}

/// What the app remembers between launches: panel sizes and visibility, the selected tab, appearance, schemes.
/// Never holds the API token (that is in the Keychain) or anything that came from the backend.
public struct UIState: Codable, Equatable, Sendable {
    public var navigatorVisible = true
    public var inspectorVisible = true
    public var debugVisible = true
    public var navigatorWidth = PanelLimits.navigatorDefault
    public var inspectorWidth = PanelLimits.inspectorDefault
    public var debugHeight = PanelLimits.debugDefault
    public var navigatorTab = NavigatorTab.devices
    public var inspectorTab = InspectorTab.attributes
    public var appearance = AppearanceChoice.system
    /// nil means: use the bundled backend, or the default address when there is none.
    public var apiURL: String?
    public var schemes: [SchemeConfig] = []
    public var selectedSchemeID: String?
    public var destinations: [DestinationKey] = []
    public var debugLayout = DebugLayout.both
    public var consoleFilter = LogFilter.all
    public var variablesMode = VariablesMode.all
    public var findScope = FindScope.devices

    public init() {}

    enum CodingKeys: String, CodingKey {
        case navigatorVisible, inspectorVisible, debugVisible, navigatorWidth, inspectorWidth, debugHeight, navigatorTab, inspectorTab
        case appearance, apiURL, schemes, selectedSchemeID, destinations, debugLayout, consoleFilter, variablesMode, findScope
    }

    /// Every key is optional so that state saved by an older or newer build still loads.
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        navigatorVisible = try c.decodeIfPresent(Bool.self, forKey: .navigatorVisible) ?? true
        inspectorVisible = try c.decodeIfPresent(Bool.self, forKey: .inspectorVisible) ?? true
        debugVisible = try c.decodeIfPresent(Bool.self, forKey: .debugVisible) ?? true
        navigatorWidth = try c.decodeIfPresent(Double.self, forKey: .navigatorWidth) ?? PanelLimits.navigatorDefault
        inspectorWidth = try c.decodeIfPresent(Double.self, forKey: .inspectorWidth) ?? PanelLimits.inspectorDefault
        debugHeight = try c.decodeIfPresent(Double.self, forKey: .debugHeight) ?? PanelLimits.debugDefault
        navigatorTab = (try? c.decodeIfPresent(NavigatorTab.self, forKey: .navigatorTab)) ?? .devices
        inspectorTab = (try? c.decodeIfPresent(InspectorTab.self, forKey: .inspectorTab)) ?? .attributes
        appearance = (try? c.decodeIfPresent(AppearanceChoice.self, forKey: .appearance)) ?? .system
        apiURL = try c.decodeIfPresent(String.self, forKey: .apiURL)
        schemes = try c.decodeIfPresent([SchemeConfig].self, forKey: .schemes) ?? []
        selectedSchemeID = try c.decodeIfPresent(String.self, forKey: .selectedSchemeID)
        destinations = try c.decodeIfPresent([DestinationKey].self, forKey: .destinations) ?? []
        debugLayout = (try? c.decodeIfPresent(DebugLayout.self, forKey: .debugLayout)) ?? .both
        consoleFilter = (try? c.decodeIfPresent(LogFilter.self, forKey: .consoleFilter)) ?? .all
        variablesMode = (try? c.decodeIfPresent(VariablesMode.self, forKey: .variablesMode)) ?? .all
        findScope = (try? c.decodeIfPresent(FindScope.self, forKey: .findScope)) ?? .devices
    }

    /// The same state with every size inside its limits (a hand-edited or corrupt file cannot make a panel vanish).
    public func clamped() -> UIState {
        var copy = self
        copy.navigatorWidth = PanelLimits.clamp(navigatorWidth, to: PanelLimits.navigator)
        copy.inspectorWidth = PanelLimits.clamp(inspectorWidth, to: PanelLimits.inspector)
        copy.debugHeight = max(PanelLimits.debugMinimum, debugHeight.isFinite ? debugHeight : PanelLimits.debugDefault)
        return copy
    }
}

public protocol UIStateStore: Sendable {
    func load() -> UIState
    func save(_ state: UIState)
}

public final class InMemoryUIStateStore: UIStateStore, @unchecked Sendable {
    private let lock = NSLock()
    private var state: UIState

    public init(_ state: UIState = UIState()) { self.state = state }

    public func load() -> UIState {
        lock.lock()
        defer { lock.unlock() }
        return state.clamped()
    }

    public func save(_ state: UIState) {
        lock.lock()
        defer { lock.unlock() }
        self.state = state
    }
}

/// Stores the state as one JSON blob in `UserDefaults`.
public final class UserDefaultsUIStateStore: UIStateStore, @unchecked Sendable {
    public static let defaultKey = "mobilelab.uiState.v1"

    private let defaults: UserDefaults
    private let key: String

    public init(defaults: UserDefaults = .standard, key: String = UserDefaultsUIStateStore.defaultKey) {
        self.defaults = defaults
        self.key = key
    }

    public func load() -> UIState {
        guard let data = defaults.data(forKey: key), let state = try? JSONDecoder().decode(UIState.self, from: data) else { return UIState() }
        return state.clamped()
    }

    public func save(_ state: UIState) {
        guard let data = try? JSONEncoder().encode(state.clamped()) else { return }
        defaults.set(data, forKey: key)
    }
}

/// Where the API token lives. The real one is the Keychain (in the app target); tests use memory.
public protocol TokenStore: Sendable {
    func token(for baseURL: String) -> String?
    func setToken(_ token: String?, for baseURL: String)
}

public final class InMemoryTokenStore: TokenStore, @unchecked Sendable {
    private let lock = NSLock()
    private var tokens: [String: String] = [:]

    public init() {}

    public func token(for baseURL: String) -> String? {
        lock.lock()
        defer { lock.unlock() }
        return tokens[baseURL]
    }

    public func setToken(_ token: String?, for baseURL: String) {
        lock.lock()
        defer { lock.unlock() }
        if let token, !token.isEmpty { tokens[baseURL] = token } else { tokens.removeValue(forKey: baseURL) }
    }
}
