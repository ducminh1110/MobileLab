import test from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Settings > General > Appearance of the web dashboard. The tables and the sanitising live in
// public/assets/js/core/appearance.js, a plain ES module that the browser and these tests both load.

const CORE = path.join(__dirname, "..", "..", "public", "assets", "js", "core");

type Accent = { id: string; label: string; light: string; dark: string };
type TextSize = { id: string; label: string; scale: number };
type Values = { theme: string; accent: string; textSize: string; reduceMotion: boolean | null; glass: string };
interface FakeRoot { dataset: Record<string, string>; style: { props: Record<string, string>; setProperty(name: string, value: string): void; getPropertyValue(name: string): string } }
interface Appearance {
  ACCENTS: Accent[];
  TEXT_SIZES: TextSize[];
  THEMES: string[];
  ACCENT_TEXT: string;
  APPEARANCE_DEFAULTS: Values;
  accentById(id: unknown): Accent;
  textSizeById(id: unknown): TextSize;
  sanitizeAppearance(raw: unknown): Values;
  reducedMotion(pref: unknown, system: boolean): boolean;
  contrastRatio(a: string, b: string): number;
  relativeLuminance(hex: string): number;
  applyAppearance(root: FakeRoot, values: unknown, systemReducedMotion?: boolean): void;
  textScale(root?: FakeRoot): number;
  scaledPx(px: number, root?: FakeRoot): number;
}

const load = async (): Promise<Appearance> => (await import(pathToFileURL(path.join(CORE, "appearance.js")).href)) as unknown as Appearance;

function fakeRoot(): FakeRoot {
  const props: Record<string, string> = {};
  return { dataset: {}, style: { props, setProperty: (name, value) => { props[name] = value; }, getPropertyValue: (name) => props[name] ?? "" } };
}

test("accent table: Blue, Purple, Pink, Red, Orange, Green and Graphite, each with a light and a dark value", async () => {
  const { ACCENTS, APPEARANCE_DEFAULTS, relativeLuminance } = await load();
  assert.deepEqual(ACCENTS.map((a) => a.id), ["blue", "purple", "pink", "red", "orange", "green", "graphite"]);
  assert.deepEqual(ACCENTS.map((a) => a.label), ["Blue", "Purple", "Pink", "Red", "Orange", "Green", "Graphite"]);
  assert.equal(APPEARANCE_DEFAULTS.accent, "blue", "Blue is the default");
  assert.equal(ACCENTS[0].light, "#0a7aff", "today's Xcode blue");
  assert.equal(ACCENTS[0].dark, "#0a84ff");
  for (const a of ACCENTS) {
    assert.match(a.light, /^#[0-9a-f]{6}$/, `${a.id} light`);
    assert.match(a.dark, /^#[0-9a-f]{6}$/, `${a.id} dark`);
    assert.ok(relativeLuminance(a.dark) > relativeLuminance(a.light), `${a.id}: the dark value is a bit brighter, like Apple's system colours`);
  }
});

test("white text on every accent is readable: at least 3:1 in light and in dark (7 x 2)", async () => {
  const { ACCENTS, ACCENT_TEXT, contrastRatio } = await load();
  assert.equal(ACCENT_TEXT, "#ffffff");
  let checked = 0;
  for (const a of ACCENTS) {
    for (const appearance of ["light", "dark"] as const) {
      const ratio = contrastRatio(ACCENT_TEXT, a[appearance]);
      assert.ok(ratio >= 3, `${a.id} ${appearance} ${a[appearance]}: ${ratio.toFixed(2)}:1 is below 3:1`);
      checked += 1;
    }
  }
  assert.equal(checked, 14);
});

test("contrast helper follows WCAG 2.x", async () => {
  const { contrastRatio, relativeLuminance } = await load();
  assert.ok(Math.abs(contrastRatio("#000000", "#ffffff") - 21) < 1e-9);
  assert.ok(Math.abs(contrastRatio("#ffffff", "#ffffff") - 1) < 1e-9);
  assert.equal(contrastRatio("#0a7aff", "#ffffff"), contrastRatio("#ffffff", "#0a7aff"), "order does not matter");
  assert.ok(Math.abs(relativeLuminance("#808080") - 0.2159) < 1e-3);
  assert.ok(Math.abs(contrastRatio("#0a7aff", "#ffffff") - 4.0) < 0.05, "Xcode blue on white is about 4:1");
  assert.throws(() => contrastRatio("blue", "#ffffff"));
  assert.throws(() => contrastRatio("#fff", "#ffffff"));
});

test("text sizes: Small, Default, Large, Larger at 0.92, 1.0, 1.1, 1.2", async () => {
  const { TEXT_SIZES, APPEARANCE_DEFAULTS, textSizeById } = await load();
  assert.deepEqual(TEXT_SIZES.map((s) => [s.id, s.label, s.scale]), [["small", "Small", 0.92], ["default", "Default", 1], ["large", "Large", 1.1], ["larger", "Larger", 1.2]]);
  assert.equal(textSizeById(APPEARANCE_DEFAULTS.textSize).scale, 1, "Default is exactly the designed density");
  assert.equal(textSizeById("nope").scale, 1);
});

test("scaled pixel sizes round to whole pixels like the CSS round(Npx * var(--ts), 1px)", async () => {
  const { scaledPx, textScale } = await load();
  const at = (scale: string): FakeRoot => { const root = fakeRoot(); root.style.setProperty("--ts", scale); return root; };
  assert.equal(textScale(fakeRoot()), 1, "no --ts yet means the designed size");
  assert.deepEqual([0.92, 1, 1.1, 1.2].map((s) => scaledPx(17, at(String(s)))), [16, 17, 19, 20], "log line height");
  assert.deepEqual([0.92, 1, 1.1, 1.2].map((s) => scaledPx(22, at(String(s)))), [20, 22, 24, 26], "row height");
});

test("stored preferences that are unknown or invalid fall back to the defaults", async () => {
  const { sanitizeAppearance, APPEARANCE_DEFAULTS } = await load();
  assert.deepEqual(APPEARANCE_DEFAULTS, { theme: "system", accent: "blue", textSize: "default", reduceMotion: null, glass: "on" });
  for (const junk of [undefined, null, 0, 1, "dark", true, [], ["purple"], () => 1]) assert.deepEqual(sanitizeAppearance(junk), APPEARANCE_DEFAULTS, `junk: ${String(junk)}`);
  assert.deepEqual(sanitizeAppearance({}), APPEARANCE_DEFAULTS);

  const bad = { theme: "sepia", accent: "magenta", textSize: "huge", reduceMotion: "yes", glass: "off" };
  assert.deepEqual(sanitizeAppearance(bad), APPEARANCE_DEFAULTS, "every field falls back on its own");
  for (const accent of ["", "Purple", "PURPLE", "__proto__", "constructor", "toString", 3, null, {}, ["purple"]]) assert.equal(sanitizeAppearance({ accent }).accent, "blue", `accent ${JSON.stringify(accent)}`);
  for (const textSize of ["", "Large", "1.2", 1.2, "__proto__", null]) assert.equal(sanitizeAppearance({ textSize }).textSize, "default", `textSize ${JSON.stringify(textSize)}`);
  for (const reduceMotion of ["true", 1, 0, {}, undefined]) assert.equal(sanitizeAppearance({ reduceMotion }).reduceMotion, null, `reduceMotion ${JSON.stringify(reduceMotion)}`);

  const good = { theme: "dark", accent: "graphite", textSize: "larger", reduceMotion: false, glass: "reduced" };
  assert.deepEqual(sanitizeAppearance(good), good, "valid values survive");
  assert.equal(sanitizeAppearance({ reduceMotion: true }).reduceMotion, true);
  assert.deepEqual(sanitizeAppearance({ accent: "pink", other: 1 }), { ...APPEARANCE_DEFAULTS, accent: "pink" }, "other keys are not carried over");
});

test("invalid preferences in localStorage do not survive loading state.js, valid ones do", async () => {
  const stored = { theme: "light", accent: "magenta", textSize: "large", reduceMotion: "yes", glass: "off", navW: 333, scheme: "Kept" };
  const holder = globalThis as unknown as { localStorage?: unknown };
  const previous = holder.localStorage;
  holder.localStorage = { getItem: () => JSON.stringify(stored), setItem: () => undefined, removeItem: () => undefined };
  try {
    const mod = (await import(pathToFileURL(path.join(CORE, "state.js")).href)) as unknown as { prefs: Record<string, unknown> };
    assert.equal(mod.prefs.theme, "light", "valid values are kept");
    assert.equal(mod.prefs.textSize, "large");
    assert.equal(mod.prefs.accent, "blue", "an unknown accent falls back");
    assert.equal(mod.prefs.reduceMotion, null, "a non-boolean override falls back to following the system");
    assert.equal(mod.prefs.glass, "on");
    assert.equal(mod.prefs.navW, 333, "other preferences are untouched");
    assert.equal(mod.prefs.scheme, "Kept");
  } finally {
    holder.localStorage = previous;
  }
});

test("reduce motion follows the operating system until the checkbox overrides it", async () => {
  const { reducedMotion } = await load();
  assert.equal(reducedMotion(null, false), false);
  assert.equal(reducedMotion(null, true), true, "no override: the system setting");
  assert.equal(reducedMotion(true, false), true, "override on");
  assert.equal(reducedMotion(false, true), false, "override off beats the system");
});

test("applyAppearance writes theme, accent, motion and the text scale onto <html>", async () => {
  const { applyAppearance } = await load();
  const root = fakeRoot();
  applyAppearance(root, { theme: "dark", accent: "purple", textSize: "large", reduceMotion: null, glass: "on" }, true);
  assert.deepEqual(root.dataset, { theme: "dark", accent: "purple", motion: "reduce" });
  assert.equal(root.style.getPropertyValue("--ts"), "1.1");

  applyAppearance(root, { theme: "system", accent: "green", textSize: "small", reduceMotion: false }, true);
  assert.equal(root.dataset.theme, undefined, "System removes the pinned theme");
  assert.equal(root.dataset.accent, "green");
  assert.equal(root.dataset.motion, "full", "the override beats the system setting");
  assert.equal(root.style.getPropertyValue("--ts"), "0.92");

  applyAppearance(root, { theme: "plaid", accent: "teal", textSize: "tiny", reduceMotion: "maybe" }, false);
  assert.deepEqual(root.dataset, { accent: "blue", motion: "full" });
  assert.equal(root.style.getPropertyValue("--ts"), "1", "invalid values apply the defaults");
});
