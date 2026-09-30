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

Files: `backend/public/assets/js/core/glass.js` (maths, maps, SVG filters, detection, `attachGlass`) and
`assets/css/glass.css` (the material and its fallbacks). No dependencies, no CDN, CSP-clean (no inline `style=`,
custom properties through the CSSOM, maps as `data:` URLs, the filters in one hidden inline `<svg>`).

* **Levels.** `<html data-glass>` is `refract` (Chromium: SVG filters work inside `backdrop-filter`), `blur` (Safari,
  Firefox, or forced: blur + saturate + tint gradient + rim + sheen, complete on its own) or `off` (opaque surfaces:
  Settings "Reduced", `prefers-reduced-transparency`, `prefers-contrast: more`, or no `backdrop-filter`). Detection is
  `CSS.supports("backdrop-filter")` plus a Chromium check plus SVG displacement support. `?glass=refract|blur|off`
  forces a level (screenshots, tests). Settings > General > Liquid Glass: On / Reduced, stored in the `glass` pref.
* **Material (CSS).** `.glass` = backdrop blur + saturate + tint gradient (lighter on top) + `::before` specular sheen +
  `::after` 1px rim light (mask-composite ring, bright at the top left) + inner glow + two-part shadow (contact + soft).
  Variants: `.glass-capsule`, `.glass-circle`, `.glass-white` (the toolbar capsule), `.glass-quiet` (jump bar and canvas
  bar controls: almost clear), `.glass-field` (filter fields), `.glass-menu` and `.glass-sheet` (text surfaces: more tint,
  more blur), `.glass-lift`, and `.glass-group` with `.gbtn` buttons (one glass shape, several buttons, hairline dividers,
  hover, pressed `aria-expanded`, press scale 0.96 with flattened tint).
* **Refraction (JS).** Per glass element, on size change (ResizeObserver, rAF-throttled): a canvas draws the vector map
  from the bevel height field (section "Second reference"), encoded R/G around 128 (`feDisplacementMap`, scale = 2 x
  peak). Displacement always points *into* the box (the backdrop exists only there), soft-clamped before the medial
  axis, and the field is extended outward at rim strength so the map has no seam. Maps and filters are cached and shared
  per size class; an element that resizes swaps its filter (refcounted). At most about 15 glass elements are on screen
  (`glassCount()`); elements above 720px on a side are blurred only.
* **Options** (`attachGlass(el, opts)` or `data-glass-*`): `shape` capsule | circle | round, `radius`, `refraction`
  (0.69), `zRadius` (40, clamped to half the short side), `pull` (4), `chroma` (0: three displacement passes at
  scales 1+c, 1, 1-c recombined with `feBlend screen`), `frost` (0: blur px of the interior; the rim band, `band` px,
  stays sharp and refracted through an edge-weight map and `feComposite arithmetic`), `bevel` pill | dome, `refract`
  (false keeps blur only), `blur` and `saturate`.
* **Used where.** Refraction only (default) on toolbar groups, capsule, tab pills, filter fields, jump bar and canvas bar
  groups; frost + chromatic aberration on menus, popovers and sheets (text surfaces keep a sharp refracted rim of 9 to
  10px and a heavily blurred, opaque-ish interior); frost on toasts; the Jump to end pill.
* **Divider drags.** `glassBusy(true)` pauses refraction while a divider is dragged (the filter is dropped from the
  cascade through `html[data-glass-busy]`, blur stays) so panels resizing under glass never regenerate maps per frame;
  refraction returns on release. `?glassbusy=0` disables the pause (used to measure its effect).
* **What was adopted from the second reference and what was not.** Adopted: the half-circle bevel height field and
  normals from the SDF gradient, dual-surface refraction with ior 1.5 folded into one strength, the centre pull, the
  dome variant, optional chromatic aberration (per-channel scales of the same map), edge-weighted blur (sharp rim,
  frosted interior), the top-biased 1.5px rim, inner glow, Fresnel-like edge brightening, two-part shadow, press
  flatten. Dropped: the WebGL2 path (a browser cannot sample live DOM content into a texture without a snapshot
  library, which we refuse: stale and a dependency), the micro-distortion noise (cost, no visible gain), the Blinn-Phong
  specular lobes (approximated by the CSS sheen because the normal is only available per pixel in a shader), glass
  seeing glass in its refraction (`backdrop-filter` of nested elements sees the lower glass already, for free).
  Not possible with `backdrop-filter`: sampling *outside* the element's box (the real lens shows the surroundings
  pulled in at the rim; ours pulls the content from inside, which reads the same at small sizes).

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
