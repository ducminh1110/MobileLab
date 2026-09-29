import Foundation

/// The six navigators, in tab-bar order. `Cmd 1` to `Cmd 6` select them.
public enum NavigatorTab: String, CaseIterable, Codable, Sendable, Identifiable {
    case devices
    case tests
    case issues
    case find
    case debug
    case reports

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .devices: return "Devices"
        case .tests: return "Tests"
        case .issues: return "Issues"
        case .find: return "Find"
        case .debug: return "Debug"
        case .reports: return "Reports"
        }
    }

    /// SF Symbol names (icons are allowed on macOS; the font is Inter).
    public var symbol: String {
        switch self {
        case .devices: return "iphone"
        case .tests: return "diamond"
        case .issues: return "exclamationmark.triangle"
        case .find: return "magnifyingglass"
        case .debug: return "cpu"
        case .reports: return "doc.text"
        }
    }

    /// 1 to 6.
    public var number: Int { (Self.allCases.firstIndex(of: self) ?? 0) + 1 }

    public static func tab(number: Int) -> NavigatorTab? {
        guard number >= 1, number <= allCases.count else { return nil }
        return allCases[number - 1]
    }

    /// What the filter bar's toggle means in this navigator; nil when there is no toggle.
    public var toggleTitle: String? {
        switch self {
        case .devices: return "Booted only"
        case .tests, .issues, .reports: return "Failed only"
        case .find, .debug: return nil
        }
    }
}

public enum ReportTab: String, CaseIterable, Codable, Sendable, Identifiable {
    case summary
    case tests
    case logs

    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .summary: return "Summary"
        case .tests: return "Tests"
        case .logs: return "Logs"
        }
    }
}

/// What the editor area shows.
public enum EditorTarget: Hashable, Sendable, Codable {
    case welcome
    case device(String)
    case job(String)
    case run(String)

    public var jobID: String? {
        if case .job(let id) = self { return id }
        return nil
    }

    public var deviceID: String? {
        if case .device(let id) = self { return id }
        return nil
    }

    public var runID: String? {
        if case .run(let id) = self { return id }
        return nil
    }
}

// MARK: - Tree

public enum TestGlyph: Equatable, Sendable {
    case notRun
    case passed
    case failed
    case running
    case skipped
    case cancelled
}

public enum StatusDot: Equatable, Sendable {
    case ready
    case booting
    case busy
    case stopped
    case shuttingDown
    case error

    public init(_ status: DeviceStatus) {
        switch status {
        case .ready: self = .ready
        case .booting, .created: self = .booting
        case .busy: self = .busy
        case .shuttingDown: self = .shuttingDown
        case .stopped, .unknown: self = .stopped
        case .error: self = .error
        }
    }

    /// Booting, shutting down and busy pulse.
    public var pulses: Bool { self == .booting || self == .shuttingDown || self == .busy }
}

public enum IssueSeverity: Equatable, Sendable {
    case testFailure
    case buildError
    case runtimeError
}

public enum NodeIcon: Equatable, Sendable {
    /// Blue group folder (a runtime, a day).
    case folder
    /// The `Simulators` root.
    case simulators
    case iphone
    case ipad
    case vm
    /// The `Runs` root.
    case runs
    case diamond(TestGlyph)
    case issue(IssueSeverity)
    case thread
    case frame
    case document
    case none
}

/// A small square badge like the `A` / `L` badges in Xcode's variables view.
public struct NodeBadge: Equatable, Sendable {
    public enum Tone: Equatable, Sendable { case pass, fail, skip, neutral }
    public var letter: String
    public var tone: Tone

    public init(_ letter: String, _ tone: Tone) {
        self.letter = letter
        self.tone = tone
    }
}

public struct TreeNode: Identifiable, Equatable, Sendable {
    public var id: String
    public var title: String
    /// Dim text right after the title.
    public var subtitle: String?
    public var icon: NodeIcon
    public var dot: StatusDot?
    public var tags: [String]
    /// Right-aligned dim text (a time, a duration).
    public var trailing: String?
    public var badge: NodeBadge?
    /// A monospaced value shown after the title (the variables view).
    public var value: String?
    public var children: [TreeNode]
    /// Children are fetched when the row is first expanded (a job's test results).
    public var lazyJobID: String?
    /// What selecting the row opens.
    public var target: EditorTarget?
    public var tab: ReportTab?
    /// Text to find in the job's log when it opens (an issue's `File.swift:42`).
    public var focus: String?
    /// Section headers (`Today`) are always expanded and not selectable.
    public var isHeader: Bool
    public var isDimmed: Bool

    public init(
        id: String, title: String, subtitle: String? = nil, icon: NodeIcon = .none, dot: StatusDot? = nil, tags: [String] = [],
        trailing: String? = nil, badge: NodeBadge? = nil, value: String? = nil, children: [TreeNode] = [], lazyJobID: String? = nil,
        target: EditorTarget? = nil, tab: ReportTab? = nil, focus: String? = nil, isHeader: Bool = false, isDimmed: Bool = false
    ) {
        self.id = id
        self.title = title
        self.subtitle = subtitle
        self.icon = icon
        self.dot = dot
        self.tags = tags
        self.trailing = trailing
        self.badge = badge
        self.value = value
        self.children = children
        self.lazyJobID = lazyJobID
        self.target = target
        self.tab = tab
        self.focus = focus
        self.isHeader = isHeader
        self.isDimmed = isDimmed
    }

    public var isExpandable: Bool { !children.isEmpty || lazyJobID != nil }
}

/// One visible row of a tree: the node plus where it sits.
public struct FlatRow: Identifiable, Equatable, Sendable {
    public var node: TreeNode
    public var depth: Int
    public var isExpanded: Bool

    public var id: String { node.id }
    public var isExpandable: Bool { node.isExpandable }
}

public enum TreeFlattener {
    /// The rows a list shows for `roots`, given which nodes are expanded. Headers are always open.
    public static func flatten(_ roots: [TreeNode], expanded: Set<String>, expandAll: Bool = false) -> [FlatRow] {
        var rows: [FlatRow] = []
        func walk(_ nodes: [TreeNode], depth: Int) {
            for node in nodes {
                let open = node.isExpandable && (expandAll || node.isHeader || expanded.contains(node.id))
                rows.append(FlatRow(node: node, depth: depth, isExpanded: open))
                if open { walk(node.children, depth: depth + 1) }
            }
        }
        walk(roots, depth: 0)
        return rows
    }

    /// Ids of every node that has children, for "expand everything" and for the initial expansion.
    public static func expandableIDs(_ roots: [TreeNode]) -> Set<String> {
        var ids: Set<String> = []
        func walk(_ nodes: [TreeNode]) {
            for node in nodes where node.isExpandable {
                ids.insert(node.id)
                walk(node.children)
            }
        }
        walk(roots)
        return ids
    }

    /// The node with this id, wherever it is.
    public static func find(_ id: String, in roots: [TreeNode]) -> TreeNode? {
        for node in roots {
            if node.id == id { return node }
            if let found = find(id, in: node.children) { return found }
        }
        return nil
    }
}

public struct NavigatorFilter: Equatable, Sendable, Codable {
    public var text: String
    /// The filter bar's toggle: booted devices only (Devices) or failed items only (Tests, Issues, Reports).
    public var onlyProblems: Bool

    public init(text: String = "", onlyProblems: Bool = false) {
        self.text = text
        self.onlyProblems = onlyProblems
    }

    public var isActive: Bool { !text.trimmingCharacters(in: .whitespaces).isEmpty || onlyProblems }
    public var needle: String { text.trimmingCharacters(in: .whitespaces) }

    public func matches(_ title: String) -> Bool {
        needle.isEmpty || title.range(of: needle, options: [.caseInsensitive, .diacriticInsensitive]) != nil
    }
}

public enum TreeFilter {
    /// Keeps the nodes whose title matches, and the ancestors of matches (so a match is reachable).
    /// A node whose own title matches keeps all its children.
    public static func apply(_ roots: [TreeNode], filter: NavigatorFilter) -> [TreeNode] {
        let needle = filter.needle
        if needle.isEmpty { return roots }
        func prune(_ node: TreeNode) -> TreeNode? {
            let selfMatches = filter.matches(node.title) || (node.subtitle.map(filter.matches) ?? false)
            if selfMatches, !node.isHeader { return node }
            let kept = node.children.compactMap(prune)
            if kept.isEmpty { return selfMatches ? node : nil }
            var copy = node
            copy.children = kept
            return copy
        }
        return roots.compactMap(prune)
    }
}
