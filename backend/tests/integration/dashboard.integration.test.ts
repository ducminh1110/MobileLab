import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { cleanup, makeApp } from "../helpers";

// The dashboard is static files under backend/public. These tests pin the rules that keep it safe and consistent:
// everything referenced is served, nothing loads from outside, the CSP is respected by the markup, the fonts are Inter
// and JetBrains Mono, and the Liquid Glass module (assets/js/core/glass.js) is served and does the maths it documents.

const PUBLIC = path.join(__dirname, "..", "..", "public");

function walk(dir: string, out: string[] = []): string[] {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full, out);
    else out.push(full);
  }
  return out;
}

const files = walk(PUBLIC);
const source = (ext: string) => files.filter((f) => f.endsWith(ext));
const rel = (f: string) => path.relative(PUBLIC, f).split(path.sep).join("/");
const read = (f: string) => fs.readFileSync(f, "utf8");
const stripComments = (text: string) => text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:"'`])\/\/.*$/gm, "$1");

async function withApp<T>(fn: (ctx: Awaited<ReturnType<typeof makeApp>>) => Promise<T>): Promise<T> {
  const ctx = await makeApp({}, { ephemeral: true });
  try {
    return await fn(ctx);
  } finally {
    await ctx.close();
    cleanup(ctx.dataDir);
  }
}

test("index.html links every stylesheet, and every linked and imported file is served with the right type", async () => {
  await withApp(async ({ app }) => {
    const page = await app.inject({ method: "GET", url: "/" });
    assert.equal(page.statusCode, 200);
    const linked = [...page.body.matchAll(/(?:href|src)="(assets\/[^"]+|favicon\.svg)"/g)].map((m) => m[1]);
    assert.ok(linked.includes("assets/css/glass.css"), "glass.css is linked");
    assert.ok(linked.includes("assets/js/main.js"), "main.js is linked");
    for (const url of linked) {
      const res = await app.inject({ method: "GET", url: `/${url}` });
      assert.equal(res.statusCode, 200, url);
      if (url.endsWith(".css")) assert.match(res.headers["content-type"] as string, /text\/css/, url);
      if (url.endsWith(".js")) assert.match(res.headers["content-type"] as string, /javascript/, url);
    }
    // every relative ES module import resolves to a file that is served
    for (const f of source(".js")) {
      for (const m of read(f).matchAll(/^\s*import\s[^;]*?from\s+"(\.[^"]+)"/gm)) {
        const target = path.resolve(path.dirname(f), m[1]);
        assert.ok(fs.existsSync(target), `${rel(f)} imports ${m[1]} which does not exist`);
        const res = await app.inject({ method: "GET", url: `/${rel(target)}` });
        assert.equal(res.statusCode, 200, `${rel(target)} is served`);
      }
    }
    // glass.js and glass.css in particular
    const js = await app.inject({ method: "GET", url: "/assets/js/core/glass.js" });
    assert.equal(js.statusCode, 200);
    assert.match(js.body, /export function attachGlass/);
    assert.match(js.body, /feDisplacementMap/);
    const css = await app.inject({ method: "GET", url: "/assets/css/glass.css" });
    assert.equal(css.statusCode, 200);
    assert.match(css.body, /data-glass="off"/, "the opaque fallback level exists");
    assert.match(css.body, /prefers-reduced-transparency/, "reduced transparency is handled");
    assert.match(css.body, /prefers-contrast/, "increased contrast is handled");
  });
});

test("the CSP is respected by the markup: no inline style attributes, inline handlers, inline scripts or eval", () => {
  const html = read(path.join(PUBLIC, "index.html"));
  assert.doesNotMatch(html, /\sstyle\s*=/i, "index.html has a style attribute");
  assert.doesNotMatch(html, /\son[a-z]+\s*=/i, "index.html has an inline event handler");
  assert.doesNotMatch(html, /<script(?![^>]*\ssrc=)[^>]*>/i, "index.html has an inline script");
  assert.doesNotMatch(html, /<style[\s>]/i, "index.html has a <style> element");
  for (const f of source(".js")) {
    const code = stripComments(read(f));
    // markup strings: a style="..." or on...="..." attribute inside a template or string literal
    assert.doesNotMatch(code, /[\s`'"]style\s*=\s*\\?["']/, `${rel(f)} renders a style attribute (use custom properties through the CSSOM)`);
    assert.doesNotMatch(code, /\son(click|change|input|keydown|keyup|load|error|submit|pointerdown|mouse\w+)\s*=\s*\\?["']/i, `${rel(f)} renders an inline event handler`);
    assert.doesNotMatch(code, /\beval\s*\(/, `${rel(f)} uses eval`);
    assert.doesNotMatch(code, /new\s+Function\s*\(/, `${rel(f)} uses new Function`);
    assert.doesNotMatch(code, /setAttribute\(\s*["']style["']/, `${rel(f)} sets the style attribute`);
    assert.doesNotMatch(code, /\.cssText\s*=|insertRule|adoptedStyleSheets/, `${rel(f)} writes style text`);
  }
});

test("nothing is loaded from outside: no CDN, no external URLs, no @import", () => {
  for (const f of files.filter((p) => /\.(html|css|js|svg)$/.test(p))) {
    const code = stripComments(read(f)).replace(/http:\/\/www\.w3\.org\/\d+\/[\w/]+/g, "");
    const external = code.match(/(?:https?:)?\/\/[a-z0-9.-]+\.[a-z]{2,}[/"'`)\s]/gi);
    assert.equal(external, null, `${rel(f)} references an external URL: ${external?.[0]}`);
    if (f.endsWith(".css")) assert.doesNotMatch(code, /@import/, `${rel(f)} uses @import`);
  }
});

test("fonts: Inter for the interface and JetBrains Mono for code, never a system font as the primary", () => {
  const families = source(".css").flatMap((f) => [...stripComments(read(f)).matchAll(/font-family\s*:\s*([^;}]+)/g)].map((m) => ({ f: rel(f), value: m[1].trim() })));
  assert.ok(families.length > 3);
  for (const { f, value } of families) {
    if (/^var\(/.test(value)) continue;
    assert.match(value, /^"(Inter|JetBrains Mono)"/, `${f}: ${value}`);
    assert.doesNotMatch(value, /system-ui|-apple-system|BlinkMacSystemFont|San Francisco|SF Pro|SF Mono|Segoe|Roboto|Helvetica/i, `${f}: ${value}`);
  }
  const fontCss = read(path.join(PUBLIC, "assets/css/fonts.css"));
  for (const face of ["InterVariable.woff2", "JetBrainsMono-Regular.woff2"]) {
    assert.match(fontCss, new RegExp(face.replace(".", "\\.")), face);
    assert.ok(fs.existsSync(path.join(PUBLIC, "assets/fonts", face)), `${face} exists`);
  }
});

test("collapse controls: toolbar toggles carry aria-expanded, aria-controls and a shortcut tooltip; the targets exist", () => {
  const toolbar = read(path.join(PUBLIC, "assets/js/ui/toolbar.js"));
  const html = read(path.join(PUBLIC, "index.html"));
  for (const [action, controls, key] of [["toggle-navigator", "navigator", "mod+0"], ["toggle-inspector", "inspector", "mod+alt+0"], ["toggle-debug", "debug", "mod+shift+y"]]) {
    const button = toolbar.split("<button").find((b) => b.includes(`data-action="${action}"`));
    assert.ok(button, `toolbar button for ${action}`);
    assert.match(button, /aria-expanded=/, `${action} has aria-expanded`);
    assert.match(button, new RegExp(`aria-controls="${controls}"`), `${action} has aria-controls`);
    assert.ok(button.includes(`keyLabel("${key}")`), `${action} tooltip shows ${key}`);
    assert.match(html, new RegExp(`id="${controls}"`), `#${controls} exists in index.html`);
  }
  const actions = read(path.join(PUBLIC, "assets/js/core/actions.js"));
  assert.match(actions, /id: "toggle-navigator"[^\n]*keys: "mod\+0"/);
  assert.match(actions, /id: "toggle-inspector"[^\n]*keys: "mod\+alt\+0"/);
  assert.match(actions, /id: "toggle-debug"[^\n]*keys: "mod\+shift\+y"/);
  const shell = read(path.join(PUBLIC, "assets/js/ui/shell.js"));
  assert.match(shell, /dblclick/, "double click on a divider toggles the panel");
  assert.match(shell, /snap/, "dragging past the minimum snaps the panel closed");
});

test("Settings offers Liquid Glass On / Reduced and it is a persisted preference", () => {
  const sheets = read(path.join(PUBLIC, "assets/js/ui/sheets.js"));
  const state = read(path.join(PUBLIC, "assets/js/core/state.js"));
  assert.match(sheets, /Liquid Glass/);
  assert.match(sheets, /data-action="glass-mode"/);
  assert.match(state, /glass: "on"/);
});

test("Settings offers every accent and text size from the shared tables, and tokens.css defines each accent", async () => {
  const appearance = (await import(pathToFileURL(path.join(PUBLIC, "assets/js/core/appearance.js")).href)) as unknown as {
    ACCENTS: { id: string; label: string; light: string; dark: string }[];
    TEXT_SIZES: { id: string }[];
  };
  const sheets = read(path.join(PUBLIC, "assets/js/ui/sheets.js"));
  const tokens = stripComments(read(path.join(PUBLIC, "assets/css/tokens.css")));

  // the sheet renders its swatches and sizes from the tables, as radio groups with accessible names
  assert.match(sheets, /ACCENTS\.map\(\(a\) => html`<button type="button" role="radio" class="swatch" data-accent="\$\{a\.id\}" aria-checked=[^>]*aria-label="\$\{a\.label\}"[^>]*data-action="accent"/);
  assert.match(sheets, /role="radiogroup" aria-label="Accent Color"/);
  assert.match(sheets, /TEXT_SIZES\.map\(\(t\) => html`<button type="button" role="radio" aria-checked=[^>]*data-action="text-size"/);
  assert.match(sheets, /role="radiogroup" aria-label="Text Size"/);
  for (const name of ["accent", "text-size", "reduce-motion", "reset-appearance"]) {
    assert.match(sheets, new RegExp(`data-action="${name}"`), `${name} is in the markup`);
    assert.match(sheets, new RegExp(`action\\("${name}"`), `${name} has a handler`);
  }
  assert.match(sheets, /<input type="checkbox" data-action="reduce-motion"[^>]*> Reduce motion/);

  // the stylesheet defines every accent the sheet offers, with the values of the table, and Blue is the default
  assert.match(tokens, /--accent:\s*var\(--accent-blue\);/);
  for (const a of appearance.ACCENTS) {
    assert.match(tokens, new RegExp(`--accent-${a.id}:\\s*light-dark\\(${a.light},\\s*${a.dark}\\);`), `--accent-${a.id} matches the table`);
    assert.match(tokens, new RegExp(`\\[data-accent="${a.id}"\\]\\s*\\{\\s*--accent:\\s*var\\(--accent-${a.id}\\);\\s*\\}`), `[data-accent="${a.id}"] selects it`);
  }
  assert.equal([...tokens.matchAll(/--accent-(?!text)[a-z]+:/g)].length, appearance.ACCENTS.length, "no accent in the stylesheet that the sheet does not offer");
  assert.equal([...tokens.matchAll(/\[data-accent="[a-z]+"\]/g)].length, appearance.ACCENTS.length);

  // the accent family is the only place the accent colours are written down
  const values = appearance.ACCENTS.flatMap((a) => [a.light, a.dark]);
  for (const f of source(".css")) {
    if (f.endsWith("tokens.css")) continue;
    const css = stripComments(read(f)).toLowerCase();
    for (const v of values) assert.ok(!css.includes(v), `${rel(f)} hard-codes the accent colour ${v}`);
  }
  assert.ok(appearance.TEXT_SIZES.length >= 4);
});

test("text size: every font size scales with --ts, and rows that hold text are rounded to whole pixels", () => {
  const tokens = stripComments(read(path.join(PUBLIC, "assets/css/tokens.css")));
  assert.match(tokens, /--ts:\s*1;/, "Default is a scale of exactly 1");
  assert.match(tokens, /--row-h:\s*round\(22px \* var\(--ts\), 1px\);/);
  assert.match(tokens, /--log-row:\s*round\(17px \* var\(--ts\), 1px\);/);
  for (const f of source(".css")) {
    const css = stripComments(read(f));
    for (const m of css.matchAll(/(?:^|[;{\s])font-size:\s*([^;}]+)/g)) {
      assert.match(m[1].trim(), /^(calc\(\d+(?:\.\d+)?px \* var\(--ts\)\)|inherit)$/, `${rel(f)} has a font size that ignores the text size: ${m[1].trim()}`);
    }
    for (const m of css.matchAll(/line-height:\s*(\d+(?:\.\d+)?px)/g)) assert.fail(`${rel(f)} has a fixed line height ${m[1]}`);
  }
});

test("reduce motion is one switch on <html data-motion>, written before the first paint and kept current by the shell", () => {
  const html = read(path.join(PUBLIC, "index.html"));
  const early = html.indexOf('src="assets/js/early.js"');
  assert.ok(early > 0 && early < html.indexOf('src="assets/js/main.js"'), "early.js is loaded before main.js");
  assert.match(html, /<script type="module" src="assets\/js\/early\.js" blocking="render"><\/script>/, "render blocking, so there is no flash of the default accent");
  assert.match(read(path.join(PUBLIC, "assets/js/early.js")), /applyAppearance\(document\.documentElement, prefs/);
  const base = stripComments(read(path.join(PUBLIC, "assets/css/base.css")));
  assert.match(base, /:root\[data-motion="reduce"\] \*,[^{]*\{\s*animation: none !important; transition: none !important;/);
  assert.match(base, /:root\[data-motion="reduce"\] \.spin \{ animation: spin 2\.4s linear infinite !important; \}/, "the spinner keeps turning, slowly");
  for (const f of source(".css")) assert.doesNotMatch(stripComments(read(f)), /prefers-reduced-motion/, `${rel(f)} reads the OS setting directly; it must go through data-motion`);
  const shell = read(path.join(PUBLIC, "assets/js/ui/shell.js"));
  assert.match(shell, /prefers-reduced-motion: reduce/);
  assert.match(shell, /if \(prefs\.reduceMotion === null\)/, "the OS setting is followed only while there is no override");
});

// ---------------------------------------------------------------------------------------------- the maths

interface Glass {
  GLASS_DEFAULTS: { refraction: number; zRadius: number; pull: number; chroma: number; frost: number; band: number; bevel: string };
  roundedBoxSdf(px: number, py: number, halfW: number, halfH: number, r: number): { sd: number; nx: number; ny: number };
  bevelHeight(d: number, zR: number): number;
  refractionAt(px: number, py: number, w: number, h: number, r: number, p?: Glass["GLASS_DEFAULTS"]): number[];
  buildMaps(w: number, h: number, r: number, params?: Glass["GLASS_DEFAULTS"], withEdge?: boolean): { data: Uint8ClampedArray; edge: Uint8ClampedArray | null; scale: number };
}
async function loadGlass(): Promise<Glass> {
  return (await import(pathToFileURL(path.join(PUBLIC, "assets/js/core/glass.js")).href)) as unknown as Glass;
}

test("glass maths: the SDF, the bevel and the displacement field behave like a convex lens", async () => {
  const g = await loadGlass();
  // SDF: negative inside, positive outside, exact on the edge, outward normals
  const inside = g.roundedBoxSdf(0, 0, 100, 20, 20);
  assert.ok(inside.sd < 0);
  assert.ok(Math.abs(g.roundedBoxSdf(100, 0, 100, 20, 20).sd) < 1e-9, "on the right edge");
  assert.ok(g.roundedBoxSdf(120, 0, 100, 20, 20).sd > 0);
  const right = g.roundedBoxSdf(100, 0, 100, 20, 20);
  assert.ok(right.nx > 0.99 && Math.abs(right.ny) < 0.01, "normal on the right edge points right");
  const top = g.roundedBoxSdf(0, -20, 100, 20, 20);
  assert.ok(top.ny < -0.99, "normal on the top edge points up");

  // bevel height: 0 outside, half circle, zR beyond
  assert.equal(g.bevelHeight(-1, 14), 0);
  assert.equal(g.bevelHeight(0, 14), 0);
  assert.ok(Math.abs(g.bevelHeight(14, 14) - 14) < 1e-9);
  assert.equal(g.bevelHeight(30, 14), 14);
  assert.ok(Math.abs(g.bevelHeight(7, 14) - Math.sqrt(7 * 21)) < 1e-9);

  // displacement of a 300x34 capsule (r = 17)
  const w = 300;
  const h = 34;
  const r = 17;
  const at = (x: number, y: number) => g.refractionAt(x - w / 2, y - h / 2, w, h, r);
  const mag = (v: number[]) => Math.hypot(v[0], v[1]);
  const [vx, vy] = at(1, 17);
  assert.ok(vx > 2, "at the left rim the backdrop is sampled from further inside (positive x): " + vx);
  assert.ok(Math.abs(vy) < 1, "and hardly moves vertically at the middle of the end cap");
  const [rx] = at(299, 17);
  assert.ok(rx < -2, "mirror image on the right rim");
  assert.ok(Math.abs(vx + rx) < 0.5, "left-right symmetry: " + vx + " vs " + rx);
  const [, topVy] = at(150, 1);
  const [, botVy] = at(150, 33);
  assert.ok(topVy > 1 && botVy < -1, "top and bottom edges pull inward");
  assert.ok(Math.abs(topVy + botVy) < 0.5, "top-bottom symmetry");
  assert.ok(mag(at(150, 17)) < 0.5, "deep inside, in the flat middle, there is (almost) no displacement: " + mag(at(150, 17)));
  // strongest at the rim, decays inward
  assert.ok(mag(at(1, 17)) > mag(at(6, 17)) && mag(at(6, 17)) > mag(at(12, 17)), "monotonic decay from the rim");
  // the backdrop only exists inside the box: no sample may land outside it
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [dx, dy] = at(x + 0.5, y + 0.5);
      assert.ok(x + 0.5 + dx >= -0.001 && x + 0.5 + dx <= w + 0.001 && y + 0.5 + dy >= -0.001 && y + 0.5 + dy <= h + 0.001, `sample of (${x},${y}) leaves the box`);
    }
  }
  // outside the shape the field continues at rim strength (no seam for the map's bilinear filtering)
  const outsideVec = g.refractionAt(-w / 2 - 3, 0, w, h, r);
  assert.ok(mag(outsideVec) > 1);
  // dome: content contracts toward the centre
  const dome = g.refractionAt(-100, 0, 300, 100, 50, { ...g.GLASS_DEFAULTS, bevel: "dome", refraction: 1 });
  assert.ok(dome[0] > 0, "dome samples toward the centre");
});

test("glass maps: neutral value 128, exact size, opaque, and a wider edge weight for the frost option", async () => {
  const g = await loadGlass();
  const { data, edge, scale } = g.buildMaps(120, 40, 20, g.GLASS_DEFAULTS, true);
  assert.equal(data.length, 120 * 40 * 4);
  assert.ok(scale > 1 && scale < 200);
  const px = (x: number, y: number) => (y * 120 + x) * 4;
  assert.ok(Math.abs(data[px(60, 20)] - 128) <= 6 && Math.abs(data[px(60, 20) + 1] - 128) <= 6, "middle pixel is (almost) neutral");
  assert.ok(data[px(1, 20)] > 140, "left rim pixel encodes a positive x offset");
  assert.ok(data[px(118, 20)] < 116, "right rim pixel encodes a negative x offset");
  for (let i = 3; i < data.length; i += 4) assert.equal(data[i], 255, "alpha stays opaque (feDisplacementMap reads unpremultiplied colours)");
  assert.ok(edge, "edge map is built when frost is requested");
  assert.ok(edge![px(1, 20)] > 200 && edge![px(60, 20)] < 5, "edge weight is 1 at the rim and 0 in the middle");
});
