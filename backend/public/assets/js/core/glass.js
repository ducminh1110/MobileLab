// Liquid Glass, our own implementation (docs/design/liquid-glass.md).
//
// The material is CSS (blur, saturate, tint gradient, rim light, specular sheen: see glass.css). This module adds the
// part CSS cannot do: refraction. For every glass element it draws a small displacement map on a canvas from a signed
// distance field of the element's shape, wraps it in an SVG filter (feImage + feDisplacementMap) and hands the filter to
// `backdrop-filter` through the custom property --glass-filter. The map is encoded as R = x offset, G = y offset around
// 128, and always points INTO the shape (the backdrop is only available inside the element's own box), so the pixels
// near the rim are pulled from further inside: content bends and compresses toward the edge like a thick convex lens.
//
//   mag(d)  = edge * exp(-d / edgeWidth) + rim * exp(-d / rimWidth) + base * (1 - exp(-d / baseWidth))
//   vector  = -normal * mag(d) * window(d)  -  (p - centre) * magnify
//   sample  = backdrop(p + vector)
//
// d is the depth below the edge, window() fades the effect out before the medial axis so it never jumps there.
//
// Everything degrades: `data-glass` on <html> is "refract" (Chromium: SVG filters work inside backdrop-filter), "blur"
// (blur + saturate + tint + rim, complete and good on its own) or "off" (opaque surfaces: reduced transparency, high
// contrast, or the Settings switch). Strict CSP safe: no inline style attributes, only CSSOM custom properties, the map is
// a data: URL (img-src allows it) and the filter lives in an inline <svg>.

const SVG_NS = "http://www.w3.org/2000/svg";
const MAX_SIDE = 720; // above this the element is blurred only; maps for huge panels are not worth their cost
const CACHE_LIMIT = 48;

export const GLASS_DEFAULTS = {
  edge: 14, // maximum displacement at the very edge, in CSS pixels
  edgeWidth: 6,
  rim: 6,
  rimWidth: 14,
  base: 0,
  baseWidth: 30,
  magnify: 0.22 // overall magnification: fraction of the shape's half thickness sampled toward the centre at the far ends
};

let defs = null; // <defs> inside the hidden <svg>
let nextId = 1;
let mode = "blur"; // effective: refract | blur | off
let userMode = "on"; // "on" | "reduced" (Settings)
let forced = ""; // "", "refract", "blur", "off" (?glass=... or setGlassForce, used for screenshots and tests)
let busy = 0;

const filters = new Map(); // key -> { id, node, refs, scale }
const maps = new Map(); // key -> { url, scale } (LRU by insertion order)
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
  const root = document.documentElement;
  root.dataset.glass = mode;
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
  busy = Math.max(0, busy + (on ? 1 : -1));
  const root = document.documentElement;
  if (busy) root.dataset.glassBusy = "1";
  else {
    delete root.dataset.glassBusy;
    for (const inst of live) inst.update(false);
  }
}

// ---------------------------------------------------------------- displacement map

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

/** The displacement vector at a point (pure function: unit-tested through the offsets it produces). */
export function refractionAt(px, py, w, h, r, p = GLASS_DEFAULTS) {
  const { sd, nx, ny } = roundedBoxSdf(px, py, w / 2, h / 2, r);
  const outside = -sd; // negative outside the shape: the field is extended outward at full edge strength so the map has no seam
  const d = Math.max(outside, 0);
  const depth = Math.min(w, h) / 2;
  const cap = depth * 0.9;
  const mag = Math.min(cap, p.edge * Math.exp(-d / p.edgeWidth) + p.rim * Math.exp(-d / p.rimWidth) + p.base * (1 - Math.exp(-d / p.baseWidth))) * (1 - smooth(depth * 0.5, depth, d));
  // magnification: sample slightly toward the centre, in proportion to the thickness of the shape (not its length)
  const k = p.magnify * depth;
  return [-nx * mag - (px / (w / 2)) * k, -ny * mag - (py / (h / 2)) * k, outside];
}

export function buildDisplacement(w, h, r, params = GLASS_DEFAULTS) {
  const vec = new Float32Array(w * h * 2);
  let peak = 0.5;
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [vx, vy] = refractionAt(x + 0.5 - w / 2, y + 0.5 - h / 2, w, h, r, params);
      const i = (y * w + x) * 2;
      vec[i] = vx;
      vec[i + 1] = vy;
      const m = Math.max(Math.abs(vx), Math.abs(vy));
      if (m > peak) peak = m;
    }
  }
  const scale = peak * 2; // feDisplacementMap: offset = scale * (channel - 0.5)
  const data = new Uint8ClampedArray(w * h * 4);
  for (let i = 0, j = 0; i < vec.length; i += 2, j += 4) {
    data[j] = Math.round(255 * (0.5 + vec[i] / scale));
    data[j + 1] = Math.round(255 * (0.5 + vec[i + 1] / scale));
    data[j + 2] = 128;
    data[j + 3] = 255;
  }
  return { data, scale };
}

function mapFor(key, w, h, r, params) {
  const hit = maps.get(key);
  if (hit) { maps.delete(key); maps.set(key, hit); return hit; }
  const { data, scale } = buildDisplacement(w, h, r, params);
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  canvas.getContext("2d").putImageData(new ImageData(data, w, h), 0, 0);
  const entry = { url: canvas.toDataURL("image/png"), scale };
  maps.set(key, entry);
  if (maps.size > CACHE_LIMIT) maps.delete(maps.keys().next().value);
  return entry;
}

// ---------------------------------------------------------------- SVG filters (one per size class, shared)

function ensureDefs() {
  if (defs) return defs;
  const svg = document.createElementNS(SVG_NS, "svg");
  svg.setAttribute("class", "glass-defs");
  svg.setAttribute("width", "0");
  svg.setAttribute("height", "0");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("focusable", "false");
  defs = document.createElementNS(SVG_NS, "defs");
  svg.appendChild(defs);
  document.body.appendChild(svg);
  return defs;
}

function acquireFilter(key, w, h, r, params) {
  let f = filters.get(key);
  if (!f) {
    const map = mapFor(key, w, h, r, params);
    const id = `lg-${nextId++}`;
    const node = document.createElementNS(SVG_NS, "filter");
    node.setAttribute("id", id);
    node.setAttribute("filterUnits", "userSpaceOnUse");
    node.setAttribute("primitiveUnits", "userSpaceOnUse");
    node.setAttribute("x", "0");
    node.setAttribute("y", "0");
    node.setAttribute("width", String(w));
    node.setAttribute("height", String(h));
    node.setAttribute("color-interpolation-filters", "sRGB");
    const image = document.createElementNS(SVG_NS, "feImage");
    image.setAttribute("x", "0");
    image.setAttribute("y", "0");
    image.setAttribute("width", String(w));
    image.setAttribute("height", String(h));
    image.setAttribute("preserveAspectRatio", "none");
    image.setAttribute("href", map.url);
    image.setAttribute("result", "map");
    const displace = document.createElementNS(SVG_NS, "feDisplacementMap");
    displace.setAttribute("in", "SourceGraphic");
    displace.setAttribute("in2", "map");
    displace.setAttribute("scale", map.scale.toFixed(3));
    displace.setAttribute("xChannelSelector", "R");
    displace.setAttribute("yChannelSelector", "G");
    node.append(image, displace);
    ensureDefs().appendChild(node);
    f = { id, node, refs: 0 };
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

/**
 * Makes `el` a glass element. Options: shape "capsule" | "circle" | "round" (default "round", radius from CSS or
 * `radius`), refract (default true), plus any of GLASS_DEFAULTS (edge, edgeWidth, rim, rimWidth, base, magnify) and
 * the CSS-level knobs blur (px), saturate, which are written as custom properties.
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
      if (!wantFx || busy) {
        if (!wantFx) this.release();
        return; // while busy the CSS hides the filter anyway; keep the stale one
      }
      const r = Math.round(radiusOf(el, o.shape, w, h, o.radius) * 2) / 2;
      const params = { ...GLASS_DEFAULTS, ...pick(o) };
      const key = `${w}x${h}|${r}|${params.edge}|${params.edgeWidth}|${params.rim}|${params.rimWidth}|${params.base}|${params.magnify}`;
      if (key === this.key) return;
      const f = acquireFilter(key, w, h, r, params);
      const prev = this.key;
      this.key = key;
      el.style.setProperty("--glass-filter", `url(#${f.id})`);
      if (prev) releaseFilter(prev);
    },
    release() {
      if (this.key) { releaseFilter(this.key); this.key = ""; }
      el.style.removeProperty("--glass-filter");
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

const pick = (o) => Object.fromEntries(Object.keys(GLASS_DEFAULTS).filter((k) => o[k] != null).map((k) => [k, o[k]]));

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
      edge: num(d.glassEdge),
      magnify: num(d.glassMagnify)
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
  const query = new URLSearchParams(location.search).get("glass");
  if (["refract", "blur", "off"].includes(query)) forced = query;
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
  window.__glass = { attachGlass, detachGlass, setGlassForce, setGlassMode, glassMode, glassCount, detectSupport, buildDisplacement, refractionAt };
}
