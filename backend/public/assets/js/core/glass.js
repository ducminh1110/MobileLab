// Liquid Glass, our own implementation (docs/design/liquid-glass.md, both references: the maths of a bevel height field
// with dual-surface refraction, optional chromatic aberration and an edge-weighted blur; the code is ours).
//
// The material is CSS (tint gradient, specular sheen, rim light, shadows: see glass.css). This module adds what CSS
// cannot do: refraction. For every glass element it draws small maps on a canvas from a signed distance field of the
// element's shape, wraps them in an SVG filter (feImage + feDisplacementMap) and hands the filter to `backdrop-filter`
// through the custom property --glass-filter.
//
//   h(d)     = sqrt(d (2 zR - d))   for 0 < d < zR, zR beyond, 0 outside   (half-circle bevel, d = depth below the edge)
//   grad h   = h'(d) * inward normal, h' by central difference (2px step)   (steep at the rim, flat in the middle)
//   refr     = h'(d) * k (2 + 0.5 h/zR) * refraction * 30px,  k = 1 - 1/ior (ior 1.5): entry + exit surface + through
//   pull     = -(p / halfSize) * refraction * 4px * smoothstep(0, zR, d)    (slight magnification toward the centre)
//   sample   = backdrop(p + refr + pull), always INSIDE the element's box (the backdrop only exists there), so the
//              displacement is soft-clamped to stay before the medial axis
//
// Map 1 encodes the vector as R = x, G = y around 128 (feDisplacementMap, scale = 2 * peak). Chromatic aberration (option)
// runs the same map three times with scales (1 + c), 1, (1 - c) for R, G, B and recombines them. Map 2 (option `frost`)
// is the edge weight: the rim stays sharp and refracted, the interior is blurred (frosted), like Apple's material.
//
// Everything degrades: `data-glass` on <html> is "refract" (Chromium: SVG filters work inside backdrop-filter), "blur"
// (blur + saturate + tint + rim, complete and good on its own) or "off" (opaque surfaces: reduced transparency, high
// contrast, or the Settings switch). Strict CSP safe: no inline style attributes, only CSSOM custom properties, the maps
// are data: URLs (img-src allows them) and the filter lives in an inline <svg>.

const SVG_NS = "http://www.w3.org/2000/svg";
const MAX_SIDE = 720; // above this the element is blurred only; maps for huge panels are not worth their cost
const CACHE_LIMIT = 48;
const IOR = 1.5;

export const GLASS_DEFAULTS = {
  refraction: 0.69, // overall strength (1 = about 30px of displacement at the rim of a thick bevel)
  zRadius: 40, // bevel depth in px; clamped to half the short side, so small controls get a proportionally tight bevel
  pull: 4, // px of magnification toward the centre at full strength
  chroma: 0, // chromatic aberration, fraction of the displacement (0.05 = subtle); costs two extra displacement passes
  frost: 0, // blur (px) of the interior; the rim stays sharp (edge-weighted mix); 0 = plain refraction, blur from CSS
  band: 0, // width (px) of the sharp rim band when frost is on; 0 = about half the bevel
  bevel: "pill" // "pill" (biconvex, entry + exit surface) or "dome" (flat bottom: content contracts toward the centre)
};

let defs = null; // <defs> inside the hidden <svg>
let nextId = 1;
let mode = "blur"; // effective: refract | blur | off
let userMode = "on"; // "on" | "reduced" (Settings)
let forced = ""; // "", "refract", "blur", "off" (?glass=... or setGlassForce, used for screenshots and tests)
let busy = 0;
let pauseOnBusy = true;

const filters = new Map(); // key -> { id, node, refs }
const maps = new Map(); // key -> { vector, edge, scale } (LRU by insertion order)
const attached = new WeakMap(); // element -> instance
const live = new Set();

// ---------------------------------------------------------------- feature detection

export function detectSupport() {
  const supportsBackdrop = typeof CSS !== "undefined" && (CSS.supports("backdrop-filter", "blur(1px)") || CSS.supports("-webkit-backdrop-filter", "blur(1px)"));
  const brands = navigator.userAgentData?.brands || [];
  const chromium = brands.some((b) => /Chromium/i.test(b.brand)) || (/\bChrome\/\d+/.test(navigator.userAgent) && !/Firefox/.test(navigator.userAgent));
  const canvas = typeof document !== "undefined" && !!document.createElement("canvas").getContext;
  const svgFilter = typeof document !== "undefined" && !!document.createElementNS && "SVGFEDisplacementMapElement" in window;
  return {
    backdrop: supportsBackdrop,
    refract: supportsBackdrop && chromium && canvas && svgFilter,
    reason: !supportsBackdrop ? "no backdrop-filter" : !chromium ? "SVG filters in backdrop-filter are only honoured by Chromium" : !svgFilter ? "no SVG displacement maps" : "ok"
  };
}

const prefersReduced = () => window.matchMedia("(prefers-reduced-transparency: reduce)").matches || window.matchMedia("(prefers-contrast: more)").matches;

function resolveMode() {
  if (forced) return forced;
  if (userMode === "reduced" || prefersReduced()) return "off";
  const s = detectSupport();
  return s.refract ? "refract" : s.backdrop ? "blur" : "off";
}

function applyMode() {
  const next = resolveMode();
  const changed = next !== mode;
  mode = next;
  document.documentElement.dataset.glass = mode;
  if (changed) for (const inst of live) inst.update(true);
}

export const glassMode = () => mode;

/** Settings switch: "on" (default) or "reduced" (like macOS Reduce Transparency). */
export function setGlassMode(value) {
  userMode = value === "reduced" ? "reduced" : "on";
  if (defs) applyMode();
}

/** Forces a mode regardless of the platform ("refract" | "blur" | "off" | ""), for screenshots and tests. */
export function setGlassForce(value) {
  forced = ["refract", "blur", "off"].includes(value) ? value : "";
  if (defs) applyMode();
}

/** Called around interactions that resize glass elements every frame (divider drags): refraction pauses, blur stays. */
export function glassBusy(on) {
  if (!pauseOnBusy) return;
  busy = Math.max(0, busy + (on ? 1 : -1));
  const root = document.documentElement;
  if (busy) root.dataset.glassBusy = "1";
  else {
    delete root.dataset.glassBusy;
    for (const inst of live) inst.update(false);
  }
}

// ---------------------------------------------------------------- the maths (pure functions)

const smooth = (a, b, x) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};

/** Signed distance to a rounded box centred on the origin (negative inside) and its outward normal. In the flat
 *  interior (where the true normal flips between the horizontal and vertical direction along a diagonal seam) the
 *  normal is blended with a soft-min so the displacement field stays continuous. */
export function roundedBoxSdf(px, py, halfW, halfH, r) {
  const qx = Math.abs(px) - (halfW - r);
  const qy = Math.abs(py) - (halfH - r);
  const ox = Math.max(qx, 0);
  const oy = Math.max(qy, 0);
  const len = Math.hypot(ox, oy);
  const sd = len + Math.min(Math.max(qx, qy), 0) - r;
  const sx = Math.sign(px) || 1;
  const sy = Math.sign(py) || 1;
  let nx;
  let ny;
  if (len > 0) { nx = sx * (ox / len); ny = sy * (oy / len); }
  else {
    const soft = 5;
    const a = Math.exp(-(halfW - Math.abs(px)) / soft);
    const b = Math.exp(-(halfH - Math.abs(py)) / soft);
    const n = Math.hypot(a, b) || 1;
    nx = (sx * a) / n;
    ny = (sy * b) / n;
  }
  return { sd, nx, ny };
}

/** The bevel height field h(d): a half circle, steep at the rim and flat beyond zR. */
export function bevelHeight(d, zR) {
  if (d <= 0) return 0;
  if (d >= zR) return zR;
  return Math.sqrt(d * (2 * zR - d));
}

export const effectiveZ = (w, h, p = GLASS_DEFAULTS) => Math.max(2, Math.min(p.zRadius, Math.min(w, h) / 2));

/** Displacement (where the backdrop is sampled from, relative to the pixel) at a point, plus the signed depth. */
export function refractionAt(px, py, w, h, r, p = GLASS_DEFAULTS) {
  const { sd, nx, ny } = roundedBoxSdf(px, py, w / 2, h / 2, r);
  const outside = -sd; // negative outside the shape: the field is extended outward at rim strength so the map has no seam
  const d = Math.max(outside, 0);
  const halfShort = Math.min(w, h) / 2;
  const zR = effectiveZ(w, h, p);
  const depth = smooth(0, zR, d);
  if (p.bevel === "dome") return [-px * p.refraction * depth * 0.35, -py * p.refraction * depth * 0.35, outside];
  const e = 2;
  const slope = (bevelHeight(d + e, zR) - bevelHeight(d - e, zR)) / (2 * e); // h'(d), the bevel steepness
  const k = 1 - 1 / IOR;
  const thick = bevelHeight(d, zR) / zR; // 0 at the rim, 1 in the flat middle
  const raw = slope * k * (2 + thick * 0.5) * p.refraction * 30 * (zR / GLASS_DEFAULTS.zRadius);
  const room = Math.max(0.5, 0.9 * (halfShort - d)); // never sample past the medial axis: the backdrop ends at the box
  const mag = room * (1 - Math.exp(-raw / room));
  const pull = p.refraction * p.pull * depth;
  return [-nx * mag - (px / (w / 2)) * pull, -ny * mag - (py / (h / 2)) * pull, outside];
}

/** Edge weight for the frosted interior: 1 at the rim (sharp, refracted), 0 in the middle (blurred). */
export function edgeWeight(depth, w, h, p = GLASS_DEFAULTS) {
  const zR = effectiveZ(w, h, p);
  return 1 - smooth(0, p.band > 0 ? p.band : Math.max(3, zR * 0.55), depth);
}

/** Builds the vector map (and, for frost, the edge-weight map). Big surfaces are sampled at half resolution: the field is
 *  smooth, the feImage stretches it back over the element's full box, and a sheet costs ~10ms instead of ~45ms. */
export function buildMaps(w, h, r, params = GLASS_DEFAULTS, withEdge = false) {
  const ds = w * h > 90000 ? 2 : 1;
  const mw = Math.ceil(w / ds);
  const mh = Math.ceil(h / ds);
  const vec = new Float32Array(mw * mh * 2);
  const edge = withEdge ? new Uint8ClampedArray(mw * mh * 4) : null;
  let peak = 0.5;
  for (let y = 0; y < mh; y += 1) {
    for (let x = 0; x < mw; x += 1) {
      const [vx, vy, d] = refractionAt((x + 0.5) * (w / mw) - w / 2, (y + 0.5) * (h / mh) - h / 2, w, h, r, params);
      const i = (y * mw + x) * 2;
      vec[i] = vx;
      vec[i + 1] = vy;
      const m = Math.max(Math.abs(vx), Math.abs(vy));
      if (m > peak) peak = m;
      if (edge) {
        const v = Math.round(255 * edgeWeight(Math.max(d, 0), w, h, params));
        const j = (y * mw + x) * 4;
        edge[j] = edge[j + 1] = edge[j + 2] = v;
        edge[j + 3] = 255;
      }
    }
  }
  const scale = peak * 2; // feDisplacementMap: offset = scale * (channel - 0.5)
  const data = new Uint8ClampedArray(mw * mh * 4);
  for (let i = 0, j = 0; i < vec.length; i += 2, j += 4) {
    data[j] = Math.round(255 * (0.5 + vec[i] / scale));
    data[j + 1] = Math.round(255 * (0.5 + vec[i + 1] / scale));
    data[j + 2] = 128;
    data[j + 3] = 255;
  }
  return { data, edge, scale, width: mw, height: mh };
}

/** Kept for tests and the proof page: the vector map only. */
export const buildDisplacement = (w, h, r, params = GLASS_DEFAULTS) => buildMaps(w, h, r, params, false);

function toUrl(data, w, h) {
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  canvas.getContext("2d").putImageData(new ImageData(data, w, h), 0, 0);
  return canvas.toDataURL("image/png");
}

function mapsFor(key, w, h, r, params, withEdge) {
  const hit = maps.get(key);
  if (hit) { maps.delete(key); maps.set(key, hit); return hit; }
  const built = buildMaps(w, h, r, params, withEdge);
  const entry = { vector: toUrl(built.data, built.width, built.height), edge: built.edge ? toUrl(built.edge, built.width, built.height) : "", scale: built.scale };
  maps.set(key, entry);
  if (maps.size > CACHE_LIMIT) maps.delete(maps.keys().next().value);
  return entry;
}

// ---------------------------------------------------------------- SVG filters (one per size class, shared)

function ensureDefs() {
  if (defs) return defs;
  const root = document.createElementNS(SVG_NS, "svg");
  root.setAttribute("class", "glass-defs");
  root.setAttribute("width", "0");
  root.setAttribute("height", "0");
  root.setAttribute("aria-hidden", "true");
  root.setAttribute("focusable", "false");
  defs = document.createElementNS(SVG_NS, "defs");
  root.appendChild(defs);
  document.body.appendChild(root);
  return defs;
}

function svg(tag, attrs, ...kids) {
  const el = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
  el.append(...kids);
  return el;
}

const CHANNEL = {
  r: "1 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 1 0",
  g: "0 0 0 0 0  0 1 0 0 0  0 0 0 0 0  0 0 0 1 0",
  b: "0 0 0 0 0  0 0 0 0 0  0 0 1 0 0  0 0 0 1 0"
};

/** The displacement stage for one input: a single map pass, or three (R, G, B at slightly different scales). */
function displaceStage(input, scale, chroma, tag) {
  const pass = (s, result) => svg("feDisplacementMap", { in: input, in2: "vmap", scale: s.toFixed(3), xChannelSelector: "R", yChannelSelector: "G", result });
  if (!chroma) return [pass(scale, tag)];
  return [
    pass(scale * (1 + chroma), `${tag}dr`), svg("feColorMatrix", { in: `${tag}dr`, type: "matrix", values: CHANNEL.r, result: `${tag}r` }),
    pass(scale, `${tag}dg`), svg("feColorMatrix", { in: `${tag}dg`, type: "matrix", values: CHANNEL.g, result: `${tag}g` }),
    pass(scale * (1 - chroma), `${tag}db`), svg("feColorMatrix", { in: `${tag}db`, type: "matrix", values: CHANNEL.b, result: `${tag}b` }),
    svg("feBlend", { in: `${tag}r`, in2: `${tag}g`, mode: "screen", result: `${tag}rg` }),
    svg("feBlend", { in: `${tag}rg`, in2: `${tag}b`, mode: "screen", result: tag })
  ];
}

function acquireFilter(key, w, h, r, params) {
  let f = filters.get(key);
  if (!f) {
    const frost = params.frost > 0;
    const m = mapsFor(key, w, h, r, params, frost);
    const id = `lg-${nextId++}`;
    const box = { x: 0, y: 0, width: w, height: h };
    const prims = [svg("feImage", { ...box, preserveAspectRatio: "none", href: m.vector, result: "vmap" })];
    if (!frost) prims.push(...displaceStage("SourceGraphic", m.scale, params.chroma, "out"));
    else {
      // sharp rim, frosted interior: mix(sharp displaced, blurred displaced, 1 - edge weight)
      prims.push(
        svg("feImage", { ...box, preserveAspectRatio: "none", href: m.edge, result: "emap" }),
        svg("feColorMatrix", { in: "emap", type: "matrix", values: "-1 0 0 0 1  0 -1 0 0 1  0 0 -1 0 1  0 0 0 1 0", result: "imap" }),
        svg("feGaussianBlur", { in: "SourceGraphic", stdDeviation: params.frost, edgeMode: "duplicate", result: "blurred" }),
        ...displaceStage("SourceGraphic", m.scale, params.chroma, "sharp"),
        ...displaceStage("blurred", m.scale, 0, "soft"),
        svg("feComposite", { in: "sharp", in2: "emap", operator: "arithmetic", k1: 1, k2: 0, k3: 0, k4: 0, result: "rim" }),
        svg("feComposite", { in: "soft", in2: "imap", operator: "arithmetic", k1: 1, k2: 0, k3: 0, k4: 0, result: "mid" }),
        svg("feComposite", { in: "rim", in2: "mid", operator: "arithmetic", k1: 0, k2: 1, k3: 1, k4: 0 })
      );
    }
    const node = svg("filter", { id, filterUnits: "userSpaceOnUse", primitiveUnits: "userSpaceOnUse", ...box, "color-interpolation-filters": "sRGB" }, ...prims);
    ensureDefs().appendChild(node);
    f = { id, node, refs: 0, frost };
    filters.set(key, f);
  }
  f.refs += 1;
  return f;
}

function releaseFilter(key) {
  const f = filters.get(key);
  if (!f) return;
  f.refs -= 1;
  if (f.refs <= 0) {
    f.node.remove();
    filters.delete(key);
  }
}

// ---------------------------------------------------------------- attaching to elements

function radiusOf(el, shape, w, h, override) {
  if (shape === "capsule" || shape === "circle") return Math.min(w, h) / 2;
  if (override != null) return Math.min(override, Math.min(w, h) / 2);
  const cs = getComputedStyle(el);
  const px = parseFloat(cs.borderTopLeftRadius);
  return Math.min(Number.isFinite(px) ? px : 12, Math.min(w, h) / 2);
}

const pick = (o) => Object.fromEntries(Object.keys(GLASS_DEFAULTS).filter((k) => o[k] != null).map((k) => [k, o[k]]));

/**
 * Makes `el` a glass element. Options: shape "capsule" | "circle" | "round" (default "round", radius from CSS or
 * `radius`), refract (default true), any of GLASS_DEFAULTS (refraction, zRadius, pull, chroma, frost, band, bevel), plus the
 * CSS-level knobs blur (px) and saturate, written as custom properties.
 */
export function attachGlass(el, options = {}) {
  const existing = attached.get(el);
  if (existing) { existing.options = { ...existing.options, ...options }; existing.update(true); return existing; }
  ensureDefs();
  el.classList.add("glass");
  const inst = {
    el,
    options: { shape: "round", refract: true, ...options },
    key: "",
    raf: 0,
    lastW: 0,
    lastH: 0,
    update(force = false) {
      const o = this.options;
      if (o.blur != null) el.style.setProperty("--glass-blur", `${o.blur}px`);
      if (o.saturate != null) el.style.setProperty("--glass-sat", String(o.saturate));
      const w = Math.round(el.offsetWidth);
      const h = Math.round(el.offsetHeight);
      if (!force && w === this.lastW && h === this.lastH) return;
      this.lastW = w;
      this.lastH = h;
      const wantFx = mode === "refract" && o.refract !== false && w >= 8 && h >= 8 && w <= MAX_SIDE && h <= MAX_SIDE;
      if (!wantFx) { this.release(); return; }
      if (busy) return; // while busy the CSS hides the filter anyway; keep the stale one
      const r = Math.round(radiusOf(el, o.shape, w, h, o.radius) * 2) / 2;
      const params = { ...GLASS_DEFAULTS, ...pick(o) };
      const key = `${w}x${h}|${r}|${params.refraction}|${params.zRadius}|${params.pull}|${params.chroma}|${params.frost}|${params.band}|${params.bevel}`;
      if (key === this.key) return;
      const f = acquireFilter(key, w, h, r, params);
      const prev = this.key;
      this.key = key;
      el.style.setProperty("--glass-filter", `url(#${f.id})`);
      // frost blurs inside the SVG filter (sharp rim, frosted interior), so the CSS blur before it is switched off
      if (f.frost) el.style.setProperty("--glass-blur", "0px");
      else if (o.blur == null) el.style.removeProperty("--glass-blur");
      if (prev) releaseFilter(prev);
    },
    release() {
      if (this.key) { releaseFilter(this.key); this.key = ""; }
      el.style.removeProperty("--glass-filter");
      if (this.options.blur == null) el.style.removeProperty("--glass-blur");
    },
    schedule() {
      if (this.raf) return;
      this.raf = requestAnimationFrame(() => { this.raf = 0; this.update(false); });
    },
    detach() {
      this.ro?.disconnect();
      if (this.raf) cancelAnimationFrame(this.raf);
      this.release();
      live.delete(this);
      attached.delete(el);
      el.classList.remove("glass");
    }
  };
  inst.ro = typeof ResizeObserver === "function" ? new ResizeObserver(() => inst.schedule()) : null;
  inst.ro?.observe(el);
  attached.set(el, inst);
  live.add(inst);
  inst.update(true);
  return inst;
}

export function detachGlass(el) {
  attached.get(el)?.detach();
}

export const glassCount = () => live.size;

// ---------------------------------------------------------------- declarative use: <div class="glass" data-glass-shape="capsule">

function scan(root) {
  const list = [];
  if (root.nodeType === 1 && root.matches("[data-glass-shape]")) list.push(root);
  if (root.querySelectorAll) list.push(...root.querySelectorAll("[data-glass-shape]"));
  for (const el of list) {
    if (attached.has(el)) continue;
    const d = el.dataset;
    const num = (v) => (v === undefined || v === "" ? undefined : Number(v));
    attachGlass(el, {
      shape: d.glassShape,
      radius: num(d.glassRadius),
      refract: d.glassRefract !== "0",
      refraction: num(d.glassRefraction),
      zRadius: num(d.glassZ),
      chroma: num(d.glassChroma),
      frost: num(d.glassFrost),
      band: num(d.glassBand),
      bevel: d.glassBevel
    });
  }
}

function forget(root) {
  const list = [];
  if (root.nodeType === 1 && attached.has(root)) list.push(root);
  if (root.querySelectorAll) list.push(...root.querySelectorAll(".glass"));
  for (const el of list) if (attached.has(el) && !el.isConnected) detachGlass(el);
}

/** Starts glass: reads the preference, watches the media queries, and attaches every [data-glass-shape] element,
 *  now and whenever markup with such elements is added. */
export function initGlass({ mode: preference = "on" } = {}) {
  userMode = preference === "reduced" ? "reduced" : "on";
  const params = new URLSearchParams(location.search);
  const query = params.get("glass");
  if (["refract", "blur", "off"].includes(query)) forced = query;
  if (params.get("glassbusy") === "0") pauseOnBusy = false;
  ensureDefs();
  applyMode();
  for (const q of ["(prefers-reduced-transparency: reduce)", "(prefers-contrast: more)"]) window.matchMedia(q).addEventListener?.("change", applyMode);
  scan(document.body);
  new MutationObserver((records) => {
    for (const rec of records) {
      for (const node of rec.addedNodes) if (node.nodeType === 1) scan(node);
      for (const node of rec.removedNodes) if (node.nodeType === 1) forget(node);
    }
  }).observe(document.body, { childList: true, subtree: true });
  window.__glass = { attachGlass, detachGlass, setGlassForce, setGlassMode, glassMode, glassCount, detectSupport, buildMaps, refractionAt, GLASS_DEFAULTS };
}
