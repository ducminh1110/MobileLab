import Foundation

/// The fonts the app ships. Inter is the only face for interface text; JetBrains Mono is for code, logs,
/// consoles and identifiers. No system font is used for text anywhere in the app.
///
/// The TTFs live in `shared/fonts` (one copy for every MobileLab app) and are added to the app bundle as resources.
/// SwiftUI asks for a face by its PostScript name, which is unambiguous for every weight.
public enum FontAssets {
    public struct Face: Equatable, Sendable {
        public var fileName: String
        public var postScriptName: String
        public var family: String
        public var weight: Int
    }

    public static let inter: [Face] = [
        Face(fileName: "Inter-Regular.ttf", postScriptName: "Inter-Regular", family: "Inter", weight: 400),
        Face(fileName: "Inter-Medium.ttf", postScriptName: "Inter-Medium", family: "Inter", weight: 500),
        Face(fileName: "Inter-SemiBold.ttf", postScriptName: "Inter-SemiBold", family: "Inter", weight: 600),
        Face(fileName: "Inter-Bold.ttf", postScriptName: "Inter-Bold", family: "Inter", weight: 700)
    ]

    public static let mono: [Face] = [
        Face(fileName: "JetBrainsMono-Regular.ttf", postScriptName: "JetBrainsMono-Regular", family: "JetBrains Mono", weight: 400),
        Face(fileName: "JetBrainsMono-Medium.ttf", postScriptName: "JetBrainsMono-Medium", family: "JetBrains Mono", weight: 500),
        Face(fileName: "JetBrainsMono-Bold.ttf", postScriptName: "JetBrainsMono-Bold", family: "JetBrains Mono", weight: 700)
    ]

    public static var all: [Face] { inter + mono }

    /// The PostScript name for a weight, choosing the closest face that exists.
    public static func postScriptName(mono useMono: Bool, weight: Int) -> String {
        let faces = useMono ? mono : inter
        return faces.min { abs($0.weight - weight) < abs($1.weight - weight) }?.postScriptName ?? faces[0].postScriptName
    }
}

/// The type scale of the design spec (section 2): size, and the weight to use.
public enum TypeScale {
    public struct Style: Equatable, Sendable {
        public var size: Double
        public var weight: Int
        public var mono: Bool
    }

    public static let body = Style(size: 13, weight: 400, mono: false)
    public static let toolbarTitle = Style(size: 13, weight: 700, mono: false)
    public static let capsule = Style(size: 12, weight: 500, mono: false)
    public static let capsuleState = Style(size: 12, weight: 600, mono: false)
    public static let capsuleDetail = Style(size: 12, weight: 400, mono: false)
    public static let jumpBar = Style(size: 11.5, weight: 400, mono: false)
    public static let inspectorHeader = Style(size: 11, weight: 700, mono: false)
    public static let label = Style(size: 11, weight: 600, mono: false)
    public static let code = Style(size: 12, weight: 400, mono: true)
    public static let codeStrong = Style(size: 12, weight: 500, mono: true)
    public static let variables = Style(size: 11.5, weight: 400, mono: false)

    /// Row heights (px) from the spec's density table.
    public static let navigatorRow: Double = 22
    public static let gutterRow: Double = 17
    public static let inspectorRow: Double = 22
    public static let toolbarControl: Double = 28
    public static let capsuleHeight: Double = 34
    public static let jumpBarHeight: Double = 28
}
