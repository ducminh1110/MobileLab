# Liquid Glass in MobileLab

Xcode 26 draws its controls (toolbar buttons, the scheme/destination capsule, floating bars, popovers, sheets)
as *Liquid Glass*: a translucent material that blurs and **refracts** whatever is behind it, bends light at its
edges, and carries a thin specular rim. This document says how MobileLab reproduces it on the web and on Linux.
Both are our own implementations. Nothing is copied from other projects.

## What we learned from `dashersw/liquid-glass-js` (MIT)

The library renders a page snapshot into a WebGL texture (html2canvas) and, per glass element, a fragment shader:

1. builds a signed distance field of the shape (rounded rectangle, circle, capsule) and its outward normal;
2. displaces the texture lookup along that normal by an intensity that is strongest at the edge and decays
   exponentially inward (`exp(-d * k)` for an "edge", a wider "rim" and a faint "base" term, plus a small boost
   at corners and a sinusoidal ripple along the rim);
3. blurs the displaced sample (Gaussian, radius about 5px), mixes in a vertical white to grey tint;
4. writes the shape mask with a 1px smoothstep for anti-aliased edges.

Reasons we do not use it as is: it needs html2canvas (a CDN script, blocked by our CSP, and a static snapshot
that goes stale as the UI updates), it only handles simple shapes, and it cannot be used from Qt. We keep its
physical model (edge-weighted refraction along the SDF normal, blur, tint, rim light) and build it on primitives
that see live content.

## Model (shared by both implementations)

For a glass shape with signed distance `d` (0 at the edge, growing inward), outward normal `n`:

```
refraction(d) = n * ( edge * exp(-d / edgeWidth) + rim * exp(-d / rimWidth) + base * (1 - exp(-d / baseWidth)) )
sample        = backdrop(p + refraction(d(p)))          // bends what is behind toward/away from the edge
color         = blur(sample, r) -> saturate -> tint gradient (top lighter, bottom darker) -> rim light
alpha         = 1 - smoothstep(-1, 1, sdf(p))
```

Rim light: a 1px inner stroke, brighter at the top-left and dimmer at the bottom-right (light comes from the
top-left), plus a soft outer shadow tinted toward the backdrop. A specular sheen (a low-opacity diagonal
gradient near the top edge) sells the curvature. Pressed state darkens and flattens the sheen. Disabled controls
lose the rim.

Default parameters (tune per element, in CSS pixels): blur 10, saturate 1.6, edge 14 (max displacement in px),
edgeWidth 6, rim 6, rimWidth 14, base 0, tint light `rgb(255 255 255 / 0.36)` and dark `rgb(30 30 34 / 0.42)`.

## Where glass is used (mirrors Xcode 26)

* The toolbar's grouped buttons (navigator toggle; Stop and Run), the capsule, and the inspector and debug toggles.
* The navigator tab bar (its pill and the selected circle), the filter bars, the jump bar controls and canvas
  bottom toolbar.
* Popovers, menus and sheets; floating pills (Jump to end, the inline log pill is *not* glass).
* The navigator and inspector panels are floating rounded panels over the window background (visible margins,
  radius about 14px), the editor is a rounded card. The window background is a soft, slightly tinted gradient so the glass has
  something to refract, like the pale-blue wallpaper tint behind the reference.

Content (tree rows, editor text, logs) is never glass: legibility first. Glass always keeps text contrast at
WCAG AA over the worst-case backdrop; if the backdrop cannot be sampled, the fallback tint is opaque enough to pass.

## Web implementation

* No dependencies, no CDN, CSP-clean (no inline `style=` in markup; set variables through CSSOM).
* Material = CSS. `backdrop-filter: blur() saturate() url(#lg-<shape>)` where the SVG filter contains an
  `feImage` displacement map plus `feDisplacementMap`. Displacement maps are generated at runtime with a small
  canvas routine (SDF, normal, intensity profile above encoded as R/G around 128), cached per size and radius,
  and attached as data URLs (allowed by our `img-src data:`). Chromium honours SVG filters in `backdrop-filter`;
  Safari and Firefox ignore the `url()` part, so the rule must be written so the **fallback (blur + saturate +
  tint + rim) is complete and good on its own**, with the refraction as a progressive enhancement
  (`@supports`/feature test in JS, set `data-glass="refract"` or `"blur"` on the root).
* Rim, sheen, shadows = layered gradients and inset shadows on the element and a `::before`/`::after`.
* API: one tiny module (`glass.js`) with `attachGlass(el, { shape: "capsule"|"circle"|"round", radius, ... })`,
  a ResizeObserver that regenerates the map when the size changes, and CSS custom properties for every
  parameter. Plain CSS classes (`.glass`, `.glass-capsule`, `.glass-circle`, `.glass-clear`) for the
  common cases.
* `prefers-reduced-transparency` and `prefers-contrast: more` switch to opaque surfaces. `prefers-color-scheme`
  swaps the tint and rim colours. A Settings toggle "Liquid Glass: On / Reduced" mirrors macOS.
* Performance: at most about 15 glass elements on screen, maps cached and shared by size, no animation of
  `backdrop-filter` parameters, `will-change` only while pressing.

## Qt (Linux) implementation

Qt Widgets has no backdrop blur, so `GlassPanel` does it explicitly:

* The window renders its background and content into an off-screen `QImage` (everything *except* glass
  widgets, via a "glass layer" flag while grabbing). Glass widgets read the region behind them from it.
* Per glass widget, on size change: compute the SDF and refraction table for the shape once (`QImage` of
  offsets, cached by size and radius). On repaint: displace-sample the backdrop through the table, blur (a
  separable box blur applied twice, or downscale-upscale, in `QImage` on the CPU, targeting under 3 ms for a
  typical toolbar element), apply saturate, tint gradient, rim and sheen with `QPainter`, mask with an
  anti-aliased path. Repaint only when the region behind it changed (window content dirty rectangles, resize,
  scroll, theme change), never on a timer.
* Fallback when the window has no usable backdrop (offscreen tests, very old Qt): tint plus rim without
  refraction. A `MOBILELAB_GLASS=off|blur|full` environment variable and a Settings switch control the level;
  "reduced transparency" follows the desktop setting when Qt exposes it.
* Shapes: capsule, circle, rounded rectangle, all through one SDF function shared with the widget mask.

## Verification

* Web: Playwright screenshots on Chromium prove the refraction (a high-contrast test pattern behind a glass
  capsule shows bending at the edges) and the fallback (`data-glass="blur"` forced) both look right; measure
  frame time while dragging a divider with many glass elements.
* Qt: offscreen screenshots with a striped backdrop show refraction at the edges; a unit test checks the SDF and
  refraction table (symmetry, zero displacement deep inside, maximum at the edge, mask anti-aliasing).
* Both: screenshots compared against the Xcode 26 reference images (glass buttons, capsule, tab bar pill).
