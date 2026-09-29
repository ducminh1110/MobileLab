import XCTest
@testable import IOSLabDashboardCore

final class PersistenceAndCommandTests: XCTestCase {
    // MARK: Shortcuts

    func testEveryCommandHasOneRowAndNoShortcutIsUsedTwice() {
        XCTAssertEqual(Set(ShortcutTable.all.map(\.command)), Set(AppCommand.allCases))
        XCTAssertEqual(ShortcutTable.all.count, AppCommand.allCases.count)
        XCTAssertTrue(ShortcutTable.conflicts.isEmpty, "conflicting shortcuts: \(ShortcutTable.conflicts)")
    }

    func testTheShortcutsOfTheDesignSpec() {
        func shortcut(_ command: AppCommand) -> String? { ShortcutTable.info(command).shortcut?.display }
        XCTAssertEqual(shortcut(.run), "\u{2318}R")
        XCTAssertEqual(shortcut(.stop), "\u{2318}.")
        XCTAssertEqual(shortcut(.toggleNavigator), "\u{2318}0")
        XCTAssertEqual(shortcut(.toggleInspector), "\u{2325}\u{2318}0")
        XCTAssertEqual(shortcut(.toggleDebugArea), "\u{21E7}\u{2318}Y")
        XCTAssertEqual(shortcut(.openQuickly), "\u{21E7}\u{2318}O")
        XCTAssertEqual(shortcut(.newSimulator), "\u{2318}N")
        XCTAssertEqual(shortcut(.clearConsole), "\u{2318}K")
        XCTAssertEqual(shortcut(.settings), "\u{2318},")
        XCTAssertEqual(shortcut(.findInNavigator), "\u{21E7}\u{2318}F")
        for tab in NavigatorTab.allCases {
            XCTAssertEqual(shortcut(ShortcutTable.navigatorCommand(tab)), "\u{2318}\(tab.number)", "\(tab.title) is Cmd \(tab.number)")
            XCTAssertEqual(ShortcutTable.tab(for: ShortcutTable.navigatorCommand(tab)), tab)
        }
        XCTAssertNil(ShortcutTable.tab(for: .run))
    }

    func testShortcutGroupsFollowMenuOrder() {
        let names = ShortcutTable.groups.map(\.name)
        XCTAssertEqual(Set(names).count, names.count)
        XCTAssertEqual(ShortcutTable.groups.flatMap(\.items).count, AppCommand.allCases.count)
        XCTAssertTrue(names.contains("Product") && names.contains("View") && names.contains("Navigate"))
    }

    func testNavigatorTabs() {
        XCTAssertEqual(NavigatorTab.allCases.map(\.number), [1, 2, 3, 4, 5, 6])
        XCTAssertEqual(NavigatorTab.tab(number: 3), .issues)
        XCTAssertNil(NavigatorTab.tab(number: 0))
        XCTAssertNil(NavigatorTab.tab(number: 7))
        XCTAssertEqual(NavigatorTab.allCases.map(\.symbol), ["iphone", "diamond", "exclamationmark.triangle", "magnifyingglass", "cpu", "doc.text"])
        XCTAssertNil(NavigatorTab.debug.toggleTitle)
        XCTAssertEqual(NavigatorTab.tests.toggleTitle, "Failed only")
    }

    // MARK: UI state

    func testDefaultsMatchTheSpec() {
        let state = UIState()
        XCTAssertEqual(state.navigatorWidth, 300)
        XCTAssertEqual(state.inspectorWidth, 300)
        XCTAssertEqual(state.debugHeight, 240)
        XCTAssertEqual(state.navigatorTab, .devices)
        XCTAssertEqual(state.appearance, .system)
        XCTAssertTrue(state.navigatorVisible && state.inspectorVisible)
        XCTAssertEqual(PanelLimits.navigator, 220...460)
        XCTAssertEqual(PanelLimits.inspector, 240...460)
        XCTAssertEqual(PanelLimits.debugMinimum, 120)
        XCTAssertEqual(PanelLimits.minimumWindow.width, 1000)
        XCTAssertEqual(PanelLimits.minimumWindow.height, 640)
    }

    func testClampingKeepsPanelsInsideTheirLimits() {
        var state = UIState()
        state.navigatorWidth = 10
        state.inspectorWidth = 9_000
        state.debugHeight = 3
        var clamped = state.clamped()
        XCTAssertEqual(clamped.navigatorWidth, 220)
        XCTAssertEqual(clamped.inspectorWidth, 460)
        XCTAssertEqual(clamped.debugHeight, 120)
        state.navigatorWidth = .nan
        state.debugHeight = .infinity
        clamped = state.clamped()
        XCTAssertEqual(clamped.navigatorWidth, 220)
        XCTAssertEqual(clamped.debugHeight.isFinite, true)
    }

    func testStateRoundTripsThroughJSON() throws {
        var state = UIState()
        state.navigatorVisible = false
        state.navigatorWidth = 333
        state.debugHeight = 180
        state.navigatorTab = .reports
        state.inspectorTab = .history
        state.appearance = .dark
        state.apiURL = "http://10.0.0.5:4000"
        state.schemes = [SchemeConfig(id: "s", name: "DemoApp", maxRetries: 1)]
        state.selectedSchemeID = "s"
        state.destinations = [DestinationKey(runtime: "r", model: "m")]
        state.debugLayout = .consoleOnly
        state.consoleFilter = .errors
        state.variablesMode = .failures
        state.findScope = .log
        let again = try JSONDecoder().decode(UIState.self, from: JSONEncoder().encode(state))
        XCTAssertEqual(again, state)
    }

    func testOlderOrCorruptStateStillLoads() throws {
        let partial = try JSONDecoder().decode(UIState.self, from: Data(#"{"navigatorWidth":250,"navigatorTab":"telepathy","appearance":"sepia","futureKey":true}"#.utf8))
        XCTAssertEqual(partial.navigatorWidth, 250)
        XCTAssertEqual(partial.navigatorTab, .devices, "an unknown tab falls back")
        XCTAssertEqual(partial.appearance, .system)
        XCTAssertEqual(partial.inspectorWidth, 300)
        XCTAssertEqual(try JSONDecoder().decode(UIState.self, from: Data("{}".utf8)), UIState())
    }

    func testUserDefaultsStoreSurvivesGarbage() throws {
        let suite = "mobilelab.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = UserDefaultsUIStateStore(defaults: defaults, key: "k")
        XCTAssertEqual(store.load(), UIState(), "nothing saved yet")

        var state = UIState()
        state.navigatorWidth = 400
        state.navigatorTab = .find
        store.save(state)
        XCTAssertEqual(store.load().navigatorWidth, 400)
        XCTAssertEqual(store.load().navigatorTab, .find)

        defaults.set(Data("not json".utf8), forKey: "k")
        XCTAssertEqual(store.load(), UIState(), "a corrupt blob must not crash or lock the app out")

        var wild = UIState()
        wild.navigatorWidth = 5
        store.save(wild)
        XCTAssertEqual(store.load().navigatorWidth, 220, "sizes are clamped on the way in and out")
    }

    func testInMemoryStores() {
        let store = InMemoryUIStateStore()
        var state = UIState()
        state.appearance = .light
        store.save(state)
        XCTAssertEqual(store.load().appearance, .light)

        let tokens = InMemoryTokenStore()
        XCTAssertNil(tokens.token(for: "http://a"))
        tokens.setToken("t1", for: "http://a")
        tokens.setToken("t2", for: "http://b")
        XCTAssertEqual(tokens.token(for: "http://a"), "t1")
        XCTAssertEqual(tokens.token(for: "http://b"), "t2")
        tokens.setToken(nil, for: "http://a")
        XCTAssertNil(tokens.token(for: "http://a"))
        tokens.setToken("", for: "http://b")
        XCTAssertNil(tokens.token(for: "http://b"))
    }

    func testUIStateNeverContainsTheToken() throws {
        let json = String(decoding: try JSONEncoder().encode(UIState()), as: UTF8.self).lowercased()
        XCTAssertFalse(json.contains("token"))
    }
}
