// Appearance preferences (Settings > General): the accent and text size tables, sanitising of stored values and the
// helpers around them. Nothing here touches the DOM at import time, so the dashboard and the test suite load the same file.
// The colours below are mirrored in tokens.css (--accent-*), which a test keeps in step.

export const ACCENTS = [
  { id: "blue", label: "Blue", light: "#0a7aff", dark: "#0a84ff" },
  { id: "purple", label: "Purple", light: "#8944ab", dark: "#bf5af2" },
  { id: "pink", label: "Pink", light: "#d6336c", dark: "#ff375f" },
  { id: "red", label: "Red", light: "#d92d20", dark: "#ff453a" },
  { id: "orange", label: "Orange", light: "#c2570c", dark: "#e96f0a" },
  { id: "green", label: "Green", light: "#248a3d", dark: "#26a644" },
  { id: "graphite", label: "Graphite", light: "#6b6b70", dark: "#8e8e93" }
];

/** Interface text and the rows that hold it scale by `scale` (the --ts custom property); Default is the designed density. */
export const TEXT_SIZES = [
  { id: "small", label: "Small", scale: 0.92 },
  { id: "default", label: "Default", scale: 1 },
  { id: "large", label: "Large", scale: 1.1 },
  { id: "larger", label: "Larger", scale: 1.2 }
];

export const THEMES = ["system", "light", "dark"];
export const ACCENT_TEXT = "#ffffff";

/** reduceMotion: null follows the operating system, true / false is the user's override. */
export const APPEARANCE_DEFAULTS = { theme: "system", accent: "blue", textSize: "default", reduceMotion: null, glass: "on" };

export const accentById = (id) => ACCENTS.find((a) => a.id === id) || ACCENTS[0];
export const textSizeById = (id) => TEXT_SIZES.find((s) => s.id === id) || TEXT_SIZES[1];

/** The appearance part of stored preferences with every unknown or invalid value replaced by its default. */
export function sanitizeAppearance(raw) {
  const src = raw && typeof raw === "object" ? raw : {};
  return {
    theme: THEMES.includes(src.theme) ? src.theme : APPEARANCE_DEFAULTS.theme,
    accent: ACCENTS.some((a) => a.id === src.accent) ? src.accent : APPEARANCE_DEFAULTS.accent,
    textSize: TEXT_SIZES.some((s) => s.id === src.textSize) ? src.textSize : APPEARANCE_DEFAULTS.textSize,
    reduceMotion: typeof src.reduceMotion === "boolean" ? src.reduceMotion : null,
    glass: src.glass === "reduced" ? "reduced" : APPEARANCE_DEFAULTS.glass
  };
}

/** What the checkbox shows: the override when there is one, otherwise the operating system's setting. */
export const reducedMotion = (pref, system) => (typeof pref === "boolean" ? pref : !!system);

// ---------------------------------------------------------------- contrast (WCAG 2.x)

function channels(hex) {
  const m = /^#([0-9a-f]{6})$/i.exec(hex);
  if (!m) throw new Error(`Expected #rrggbb, got ${hex}`);
  const n = Number.parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

export function relativeLuminance(hex) {
  const [r, g, b] = channels(hex).map((c) => {
    const v = c / 255;
    return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

export function contrastRatio(a, b) {
  const [hi, lo] = [relativeLuminance(a), relativeLuminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

// ---------------------------------------------------------------- applying

/** Writes the appearance onto <html>: data-theme, data-accent, data-motion and the --ts text scale (all through the DOM
 *  and CSSOM, which the CSP allows). Called before the first paint (early.js) and again whenever Settings changes. */
export function applyAppearance(root, values, systemReducedMotion = false) {
  const a = sanitizeAppearance(values);
  if (a.theme === "system") delete root.dataset.theme;
  else root.dataset.theme = a.theme;
  root.dataset.accent = a.accent;
  root.dataset.motion = reducedMotion(a.reduceMotion, systemReducedMotion) ? "reduce" : "full";
  root.style.setProperty("--ts", String(textSizeById(a.textSize).scale));
}

/** The text scale currently applied. */
export function textScale(root = document.documentElement) {
  const value = Number.parseFloat(root.style.getPropertyValue("--ts"));
  return value > 0 ? value : 1;
}

/** A designed pixel size at the current text size, rounded like the CSS round(Npx * var(--ts), 1px) (log rows are windowed in JS). */
export const scaledPx = (px, root) => Math.round(px * textScale(root));
