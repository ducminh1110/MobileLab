import Foundation

public struct KeyModifiers: OptionSet, Hashable, Sendable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    public static let command = KeyModifiers(rawValue: 1 << 0)
    public static let shift = KeyModifiers(rawValue: 1 << 1)
    public static let option = KeyModifiers(rawValue: 1 << 2)
    public static let control = KeyModifiers(rawValue: 1 << 3)
}

public struct Shortcut: Hashable, Sendable {
    /// One character: `r`, `.`, `,`, `0` ... `6`.
    public var key: Character
    public var modifiers: KeyModifiers

    public init(_ key: Character, _ modifiers: KeyModifiers = .command) {
        self.key = key
        self.modifiers = modifiers
    }

    /// `⌘R`, `⇧⌘O`, `⌥⌘0`: modifiers in the order macOS menus print them (control, option, shift, command).
    public var display: String {
        var text = ""
        if modifiers.contains(.control) { text += "\u{2303}" }
        if modifiers.contains(.option) { text += "\u{2325}" }
        if modifiers.contains(.shift) { text += "\u{21E7}" }
        if modifiers.contains(.command) { text += "\u{2318}" }
        return text + String(key).uppercased()
    }
}

public enum AppCommand: String, CaseIterable, Sendable, Identifiable {
    case run
    case stop
    case toggleNavigator
    case toggleInspector
    case toggleDebugArea
    case showDevices
    case showTests
    case showIssues
    case showFind
    case showDebug
    case showReports
    case openQuickly
    case newSimulator
    case clearConsole
    case settings
    case findInNavigator
    case refresh
    case shortcuts

    public var id: String { rawValue }
}

public struct CommandInfo: Sendable, Identifiable {
    public var command: AppCommand
    public var title: String
    public var group: String
    public var shortcut: Shortcut?

    public var id: String { command.rawValue }
}

/// The one list of keyboard shortcuts. The menu bar is built from it and so is the Keyboard Shortcuts sheet,
/// so the two cannot disagree.
public enum ShortcutTable {
    public static let all: [CommandInfo] = [
        CommandInfo(command: .run, title: "Run", group: "Product", shortcut: Shortcut("r")),
        CommandInfo(command: .stop, title: "Stop", group: "Product", shortcut: Shortcut(".")),
        CommandInfo(command: .refresh, title: "Refresh", group: "Product", shortcut: nil),
        CommandInfo(command: .toggleNavigator, title: "Navigator", group: "View", shortcut: Shortcut("0")),
        CommandInfo(command: .toggleInspector, title: "Inspectors", group: "View", shortcut: Shortcut("0", [.command, .option])),
        CommandInfo(command: .toggleDebugArea, title: "Debug Area", group: "View", shortcut: Shortcut("y", [.command, .shift])),
        CommandInfo(command: .showDevices, title: "Show Devices Navigator", group: "Navigate", shortcut: Shortcut("1")),
        CommandInfo(command: .showTests, title: "Show Tests Navigator", group: "Navigate", shortcut: Shortcut("2")),
        CommandInfo(command: .showIssues, title: "Show Issues Navigator", group: "Navigate", shortcut: Shortcut("3")),
        CommandInfo(command: .showFind, title: "Show Find Navigator", group: "Navigate", shortcut: Shortcut("4")),
        CommandInfo(command: .showDebug, title: "Show Debug Navigator", group: "Navigate", shortcut: Shortcut("5")),
        CommandInfo(command: .showReports, title: "Show Reports Navigator", group: "Navigate", shortcut: Shortcut("6")),
        CommandInfo(command: .openQuickly, title: "Open Quickly\u{2026}", group: "Navigate", shortcut: Shortcut("o", [.command, .shift])),
        CommandInfo(command: .findInNavigator, title: "Find in Navigator", group: "Find", shortcut: Shortcut("f", [.command, .shift])),
        CommandInfo(command: .newSimulator, title: "New Simulator\u{2026}", group: "File", shortcut: Shortcut("n")),
        CommandInfo(command: .clearConsole, title: "Clear Console", group: "Debug", shortcut: Shortcut("k")),
        CommandInfo(command: .settings, title: "Settings\u{2026}", group: "MobileLab", shortcut: Shortcut(",")),
        CommandInfo(command: .shortcuts, title: "Keyboard Shortcuts", group: "Help", shortcut: Shortcut("/", [.command, .shift]))
    ]

    public static func info(_ command: AppCommand) -> CommandInfo {
        all.first { $0.command == command } ?? CommandInfo(command: command, title: command.rawValue, group: "", shortcut: nil)
    }

    public static func navigatorCommand(_ tab: NavigatorTab) -> AppCommand {
        switch tab {
        case .devices: return .showDevices
        case .tests: return .showTests
        case .issues: return .showIssues
        case .find: return .showFind
        case .debug: return .showDebug
        case .reports: return .showReports
        }
    }

    public static func tab(for command: AppCommand) -> NavigatorTab? {
        NavigatorTab.allCases.first { navigatorCommand($0) == command }
    }

    /// Groups in menu order, for the shortcuts sheet.
    public static var groups: [(name: String, items: [CommandInfo])] {
        var order: [String] = []
        for item in all where !order.contains(item.group) { order.append(item.group) }
        return order.map { name in (name, all.filter { $0.group == name }) }
    }

    /// Shortcuts used by more than one command (there should be none).
    public static var conflicts: [Shortcut] {
        var seen: Set<Shortcut> = []
        var duplicates: [Shortcut] = []
        for shortcut in all.compactMap(\.shortcut) where !seen.insert(shortcut).inserted { duplicates.append(shortcut) }
        return duplicates
    }
}
