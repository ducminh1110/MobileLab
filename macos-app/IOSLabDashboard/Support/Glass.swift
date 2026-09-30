import SwiftUI

/// The shapes glass is drawn in.
enum GlassShape {
    case capsule
    case circle
    case rounded(CGFloat)

    var anyShape: AnyShape {
        switch self {
        case .capsule: return AnyShape(Capsule(style: .continuous))
        case .circle: return AnyShape(Circle())
        case .rounded(let radius): return AnyShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
        }
    }
}

/// One view modifier for every glass surface, so the rest of the code does not care which OS it runs on.
///
/// macOS 26 and newer: the native Liquid Glass (`glassEffect`), which refracts what is behind it.
/// Older systems (and builds made with an older SDK): a regular material with the rim light of the design spec
/// (docs/design/liquid-glass.md): a 1 px stroke that is brighter at the top left, plus a soft two-part shadow.
struct MLGlass: ViewModifier {
    var shape: GlassShape
    var tint: Color?
    var interactive: Bool
    var shadow: Bool
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency

    func body(content: Content) -> some View {
        #if compiler(>=6.2)
        if #available(macOS 26, *), !reduceTransparency {
            content.glassEffect(glass, in: shape.anyShape)
        } else {
            fallback(content)
        }
        #else
        fallback(content)
        #endif
    }

    #if compiler(>=6.2)
    @available(macOS 26, *)
    private var glass: Glass {
        var value = Glass.regular
        if let tint { value = value.tint(tint) }
        if interactive { value = value.interactive() }
        return value
    }
    #endif

    @ViewBuilder
    private func fallback(_ content: Content) -> some View {
        let outline = shape.anyShape
        content
            .background {
                ZStack {
                    if reduceTransparency {
                        outline.fill(Theme.capsule)
                    } else {
                        outline.fill(.regularMaterial)
                    }
                    if let tint { outline.fill(tint.opacity(0.18)) }
                    // Specular sheen: a faint white gradient at the top edge.
                    outline.fill(LinearGradient(colors: [Color.white.opacity(0.22), Color.white.opacity(0)], startPoint: .top, endPoint: .center))
                }
            }
            .overlay {
                // Stroked twice as wide and clipped to the shape, so only the inner half (the rim) shows.
                outline.stroke(
                    LinearGradient(
                        colors: [Color.white.opacity(0.75), Color.white.opacity(0.12), Color.black.opacity(0.10)],
                        startPoint: .topLeading, endPoint: .bottomTrailing
                    ),
                    lineWidth: 1.5
                )
                .clipShape(outline)
            }
            .shadow(color: .black.opacity(shadow ? 0.14 : 0), radius: 3, x: 0, y: 0.5)
            .shadow(color: .black.opacity(shadow ? 0.10 : 0), radius: 10, x: 0, y: 3)
    }
}

extension View {
    /// Liquid Glass (native on macOS 26, material and rim light before that).
    func mlGlass(_ shape: GlassShape = .capsule, tint: Color? = nil, interactive: Bool = false, shadow: Bool = true) -> some View {
        modifier(MLGlass(shape: shape, tint: tint, interactive: interactive, shadow: shadow))
    }
}

/// Groups glass shapes so that on macOS 26 they blend and refract each other like one piece of glass.
struct MLGlassContainer<Content: View>: View {
    var spacing: CGFloat = 8
    @ViewBuilder var content: Content

    var body: some View {
        #if compiler(>=6.2)
        if #available(macOS 26, *) {
            GlassEffectContainer(spacing: spacing) { content }
        } else {
            content
        }
        #else
        content
        #endif
    }
}

/// A floating rounded panel over the window background (navigator, editor card, inspector).
struct PanelBackground: ViewModifier {
    var fill: Color
    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: Metrics.panelRadius, style: .continuous)
        content
            .background(fill, in: shape)
            .clipShape(shape)
            .overlay(shape.strokeBorder(Theme.divider.opacity(0.55), lineWidth: 0.5))
            .shadow(color: .black.opacity(0.10), radius: 8, x: 0, y: 2)
    }
}

extension View {
    func panel(_ fill: Color) -> some View { modifier(PanelBackground(fill: fill)) }
}
