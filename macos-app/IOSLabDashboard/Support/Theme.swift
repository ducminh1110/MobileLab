import SwiftUI
import AppKit
#if canImport(IOSLabDashboardCore)
import IOSLabDashboardCore
#endif

extension NSColor {
    /// A colour that follows the effective appearance (light / dark), from two hex values.
    convenience init(light: UInt32, dark: UInt32, lightAlpha: CGFloat = 1, darkAlpha: CGFloat = 1) {
        self.init(name: nil, dynamicProvider: { appearance in
            let isDark = appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
            let hex = isDark ? dark : light
            return NSColor(
                srgbRed: CGFloat((hex >> 16) & 0xff) / 255,
                green: CGFloat((hex >> 8) & 0xff) / 255,
                blue: CGFloat(hex & 0xff) / 255,
                alpha: isDark ? darkAlpha : lightAlpha
            )
        })
    }
}

extension Color {
    init(light: UInt32, dark: UInt32, lightAlpha: CGFloat = 1, darkAlpha: CGFloat = 1) {
        self.init(nsColor: NSColor(light: light, dark: dark, lightAlpha: lightAlpha, darkAlpha: darkAlpha))
    }
}

/// Colour tokens of the design spec (docs/design/xcode-interface.md, section 3), named like the spec names them.
enum Theme {
    static let window = Color(light: 0xf5f5f7, dark: 0x2a2a2d)
    /// The tinted backdrop the glass and the floating panels sit on (pale blue wash in light, like the reference).
    static let windowTintTop = Color(light: 0xe3edf9, dark: 0x2a3140)
    static let windowTintBottom = Color(light: 0xf1f1f6, dark: 0x27272c)
    static let sidebar = Color(light: 0xf3f5f7, dark: 0x262629)
    static let editor = Color(light: 0xffffff, dark: 0x1f1f24)
    static let gutter = Color(light: 0xfbfbfc, dark: 0x1f1f24)
    static let gutterText = Color(light: 0xb4b6ba, dark: 0x5f6068)
    static let divider = Color(light: 0xe4e4e8, dark: 0x3a3a3f)
    static let text = Color(light: 0x1d1d1f, dark: 0xececee)
    static let textSecondary = Color(light: 0x6c6c72, dark: 0xa0a0a8)
    static let textTertiary = Color(light: 0xa1a1a8, dark: 0x6f6f78)
    static let accent = Color(light: 0x0a7aff, dark: 0x0a84ff)
    static let accentText = Color.white
    static let selection = Color(light: 0xdcdde1, dark: 0x3c3c42)
    static let capsule = Color(light: 0xffffff, dark: 0x3b3b3f)
    static let folder = Color(light: 0x5cb0f5, dark: 0x5cb0f5)
    static let field = Color(light: 0xefeff1, dark: 0x333338)
    static let debugBar = Color(light: 0xf2f2f4, dark: 0x2c2c30)
    static let console = Color(light: 0xeaf1e8, dark: 0x1d2a22)
    static let consoleFooter = Color(light: 0xdbe7d9, dark: 0x17211b)
    static let lineCurrent = Color(light: 0xe5f7e8, dark: 0x26372c)
    static let lineError = Color(light: 0xfdeceb, dark: 0x3a2426)
    static let pass = Color(light: 0x30b356, dark: 0x32d15b)
    static let fail = Color(light: 0xff3b30, dark: 0xff453a)
    static let warn = Color(light: 0xff9f0a, dark: 0xffb340)
    static let running = accent
    static let breakpoint = Color(light: 0x097dfe, dark: 0x0a84ff)

    // Log syntax
    static let synComment = Color(light: 0x5d6c79, dark: 0x6c7986)
    static let synKeyword = Color(light: 0xad3da4, dark: 0xfc5fa3)
    static let synString = Color(light: 0xd12f1b, dark: 0xfc6a5d)
    static let synNumber = Color(light: 0x272ad8, dark: 0xd0bf69)
    static let synType = Color(light: 0x703daa, dark: 0xd0a8ff)
    static let synIdentifier = Color(light: 0x326d74, dark: 0x67b7a4)

    static func color(_ style: TokenStyle) -> Color {
        switch style {
        case .plain: return text
        case .comment: return synComment
        case .keyword: return synKeyword
        case .string: return synString
        case .number: return synNumber
        case .type: return synType
        case .identifier: return synIdentifier
        case .error: return fail
        case .success: return pass
        case .warning: return warn
        }
    }

    static func color(_ tone: StatusTone) -> Color {
        switch tone {
        case .neutral: return textSecondary
        case .success: return pass
        case .failure: return fail
        case .running: return accent
        case .warning: return warn
        case .disconnected: return fail
        }
    }

    static func color(_ dot: StatusDot) -> Color {
        switch dot {
        case .ready: return pass
        case .booting, .shuttingDown: return warn
        case .busy: return accent
        case .stopped: return textTertiary
        case .error: return fail
        }
    }

    static func color(_ glyph: TestGlyph) -> Color {
        switch glyph {
        case .passed: return pass
        case .failed: return fail
        case .running: return accent
        case .notRun, .skipped, .cancelled: return textTertiary
        }
    }

    static func color(_ tone: NodeBadge.Tone) -> Color {
        switch tone {
        case .pass: return pass
        case .fail: return fail
        case .skip: return textTertiary
        case .neutral: return accent
        }
    }

    /// Window background: a soft tinted wash so the glass has something to refract.
    static var windowBackground: LinearGradient {
        LinearGradient(colors: [windowTintTop, windowTintBottom], startPoint: .topLeading, endPoint: .bottomTrailing)
    }

    /// Applies the Settings > Appearance override to every window of the app.
    @MainActor
    static func apply(_ choice: AppearanceChoice) {
        switch choice {
        case .system: NSApp.appearance = nil
        case .light: NSApp.appearance = NSAppearance(named: .aqua)
        case .dark: NSApp.appearance = NSAppearance(named: .darkAqua)
        }
    }
}

enum Metrics {
    static let panelRadius: CGFloat = 14
    static let panelGap: CGFloat = 8
    static let rowHeight = CGFloat(TypeScale.navigatorRow)
    static let gutterRow = CGFloat(TypeScale.gutterRow)
    static let controlHeight = CGFloat(TypeScale.toolbarControl)
    static let capsuleHeight = CGFloat(TypeScale.capsuleHeight)
    static let jumpBarHeight = CGFloat(TypeScale.jumpBarHeight)
    static let tabBarHeight: CGFloat = 44
    static let inspectorTabBarHeight: CGFloat = 32
    static let filterBarHeight: CGFloat = 34
    static let debugBarHeight: CGFloat = 28
    static let indent: CGFloat = 14
}
