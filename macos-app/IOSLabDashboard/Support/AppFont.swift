import SwiftUI
import AppKit
import CoreText
#if canImport(IOSLabDashboardCore)
import IOSLabDashboardCore
#endif

/// The single place fonts come from. Inter for interface text, JetBrains Mono for code, logs and identifiers.
/// No `.system` font is used for text anywhere in the app (SF Symbols are icons, not text).
enum AppFont {
    /// Registers every TTF of `FontAssets.all` for this process. Called once at launch, before the first view exists.
    /// Returns the names of the files that could not be registered (empty when everything is fine).
    @discardableResult
    static func registerAll(bundle: Bundle = .main) -> [String] {
        var failed: [String] = []
        var urls: [URL] = []
        for face in FontAssets.all {
            let base = (face.fileName as NSString).deletingPathExtension
            if let url = bundle.url(forResource: base, withExtension: "ttf")
                ?? bundle.url(forResource: base, withExtension: "ttf", subdirectory: "Fonts") {
                urls.append(url)
            } else {
                failed.append(face.fileName)
            }
        }
        guard !urls.isEmpty else { return failed }
        var errors: Unmanaged<CFArray>?
        let ok = CTFontManagerRegisterFontsForURLs(urls as CFArray, .process, &errors)
        if !ok {
            // Registering an already registered font reports an error too; only report faces that still cannot be created.
            for face in FontAssets.all where NSFont(name: face.postScriptName, size: 12) == nil {
                failed.append(face.fileName)
            }
        }
        errors?.release()
        return failed
    }

    /// True when Inter really resolves (used by the Settings > About line and by a launch-time assertion in Debug).
    static var isInterAvailable: Bool { NSFont(name: "Inter-Regular", size: 13) != nil }

    static func inter(_ size: CGFloat, weight: Int = 400) -> Font {
        .custom(FontAssets.postScriptName(mono: false, weight: weight), fixedSize: size)
    }

    static func mono(_ size: CGFloat, weight: Int = 400) -> Font {
        .custom(FontAssets.postScriptName(mono: true, weight: weight), fixedSize: size)
    }

    static func style(_ style: TypeScale.Style) -> Font {
        style.mono ? mono(CGFloat(style.size), weight: style.weight) : inter(CGFloat(style.size), weight: style.weight)
    }

    // Named styles from the design spec (section 2).
    static let body = style(TypeScale.body)
    static let bodyMedium = inter(13, weight: 500)
    static let toolbarTitle = style(TypeScale.toolbarTitle)
    static let capsule = style(TypeScale.capsule)
    static let capsuleState = style(TypeScale.capsuleState)
    static let capsuleDetail = style(TypeScale.capsuleDetail)
    static let jumpBar = style(TypeScale.jumpBar)
    static let sectionHeader = style(TypeScale.inspectorHeader)
    static let label = style(TypeScale.label)
    static let small = inter(11)
    static let code = style(TypeScale.code)
    static let codeStrong = style(TypeScale.codeStrong)
    static let variables = style(TypeScale.variables)
    static let variablesMono = mono(11.5)
    static let title = inter(22, weight: 700)
    static let heading = inter(15, weight: 600)

    /// AppKit counterpart for the few places that need an `NSFont` (text measuring, tooltips).
    static func nsFont(_ size: CGFloat, weight: Int = 400, mono useMono: Bool = false) -> NSFont {
        NSFont(name: FontAssets.postScriptName(mono: useMono, weight: weight), size: size) ?? NSFont.systemFont(ofSize: size)
    }
}
