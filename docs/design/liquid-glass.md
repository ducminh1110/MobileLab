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

## Second reference: `ybouane/liquidglass` (MIT), the richer model

A more physical model, and the one to prefer when the two disagree. Same caveats (html-to-image capture plus
WebGL, a snapshot rather than live content), so we take the maths, not the code, and write our own:

* **Bevel height field.** With `d` the distance inside the edge and `zR` the bevel "z radius" (about 40px for
  a big pill, scale it with the element, never more than half its short side), the surface height is
  `h(d) = sqrt(d * (2 zR - d))` for `0 < d < zR`, `zR` beyond, and 0 outside: a half-circle profile that is
  steep at the edge and flat in the middle. The surface normal is `N = normalize(-grad h, 1)`, with the gradient
  taken by central differences of the SDF (2px step). This replaces the `exp(-d/w)` intensity profile above:
  refraction now follows the geometry of a real lens and is smooth everywhere.
* **Refraction.** Snell-style dual surface with `ior = 1.5`, `k = 1 - 1/ior`:
  `refr = (grad h * k) [exit] + (grad h * k) [entry] + (grad h * k) * thickness/(2 zR) * 0.5 [through]`,
  scaled by a `refraction` strength (0.69 default, about 30px at full) and a slight pull toward the centre
  `-p/halfSize * strength * 4px * smoothstep(0, zR, d)`. A `dome` variant (flat bottom: only the exit surface,
  content contracts toward the centre) is the magnifier look.
* **Chromatic aberration.** Sample R, G, B at `base + c`, `base`, `base - c` where `c = N.xy * amount * 18px *
  (0.3 + 0.7 * edgeWeight)`; small (0.05 default) so it reads as glass, not as a glitch.
* **Blur mixed by edge.** Blurred backdrop in the middle, blending toward the sharp one at the rim
  (`mix(sharp, blur, 1 - 0.15 * edge)`), so the refracted edge stays crisp while the interior is frosted.
* **Light.** Fresnel `pow(1 - |N.z|, 4)` brightens grazing angles (mix toward white by 0.2 of it); specular is a
  sum of Blinn-Phong lobes from a few fixed directions (a main one from the upper right, a weak fill from the lower
  left, a broad soft one, and a sharp one from the top), each `pow(max(dot(N,H),0), 50..120)`; an inner stroke
  1.5px wide, stronger toward the top (`0.4 + 0.6 * topBias`); a soft inner glow within 5px of the edge; a faint
  environment reflection `(N.y*0.5+0.5) * fresnel * 0.08`.
* **Shadow.** Drawn outside the shape from an offset SDF: a wide Gaussian-like falloff (`exp(-d^2/spread^2)`,
  spread about 10px, weight 0.65) plus a tight contact shadow (weight 0.35), alpha about 0.3, offset 1px down.
* **Interaction.** Buttons lift slightly on hover and press flattens the bevel (`zR` and specular drop) for
  about 120ms; glass over glass sees the lower glass in its refraction (draw order = z-order).

Defaults to start from (tune against the reference screenshots): refraction 0.69, chromatic 0.05, edge highlight
0.05 to 0.3, fresnel 1.0, specular 0.3 to 0.6, zRadius 40 (capsule) / 14 (small buttons), blur 0.25, tint 0 to
0.1, shadow 0.30.

### What this changes for each implementation

* **Web.** The displacement map is built from the height-field normals instead of the exponential profile
  (canvas, per size/radius, cached). Chromatic aberration needs per-channel displacement: use three
  `feDisplacementMap` passes with slightly different scales into an `feColorMatrix`-isolated R, G and B,
  recombined with `feBlend mode="screen"` or `feComposite arithmetic`, and keep it optional (it is the first
  thing to drop for speed). Fresnel, specular lobes, the inner stroke and the shadow become layered gradients
  and inset shadows driven by the same parameters. For *large hero surfaces* (a sheet or the floating panels) an
  optional WebGL2 path may compute the whole model live from a captured texture, but only if it can sample live
  content without html2canvas-style snapshots; otherwise stay with the SVG-filter path. Do not add any
  dependency.
* **Qt.** The precomputed table stores per-pixel refraction offsets, per-channel offsets for chromatic
  aberration, the normal, the Fresnel and specular terms and the shadow alpha, all functions of the shape only,
  so a repaint is: sample the backdrop through the table (three samples per pixel when aberration is on), blend
  with the blurred copy by the edge weight, add the precomputed light layers, mask. This makes repaints cheap
  and lets the table be shared by all widgets of the same size.

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
