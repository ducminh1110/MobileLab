// Small, dependency free helpers shared by every module. Nothing in here touches the DOM at import time.

export const $ = (selector, root = document) => root.querySelector(selector);
export const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));

const ESCAPES = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };
export function esc(value) {
  return String(value ?? "").replace(/[&<>"']/g, (c) => ESCAPES[c]);
}

/** Marks a string as already-safe markup so the `html` tag does not escape it again. */
export class Raw {
  constructor(text) { this.text = text; }
  toString() { return this.text; }
}
export const raw = (text) => new Raw(text);

function part(value) {
  if (value == null || value === false || value === true) return "";
  if (value instanceof Raw) return value.text;
  if (Array.isArray(value)) return value.map(part).join("");
  return esc(value);
}

/** Tagged template: every interpolation is escaped unless it is a Raw (the result of another `html`). */
export function html(strings, ...values) {
  let out = strings[0];
  for (let i = 0; i < values.length; i += 1) out += part(values[i]) + strings[i + 1];
  return new Raw(out);
}

export const isMac = typeof navigator !== "undefined" && /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || "");

export function debounce(fn, wait, maxWait = wait * 4) {
  let timer = 0;
  let first = 0;
  return (...args) => {
    const now = Date.now();
    if (!first) first = now;
    clearTimeout(timer);
    const run = () => { first = 0; timer = 0; fn(...args); };
    if (now - first >= maxWait) run();
    else timer = setTimeout(run, wait);
  };
}

export const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

// ---------------------------------------------------------------- persistence (per viewer, never required)

export const storage = {
  get(key, fallback) {
    try {
      const value = localStorage.getItem(key);
      return value === null ? fallback : JSON.parse(value);
    } catch { return fallback; }
  },
  getRaw(key, fallback = "") {
    try { return localStorage.getItem(key) ?? fallback; } catch { return fallback; }
  },
  set(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* private mode: keep working without it */ }
  },
  setRaw(key, value) {
    try { localStorage.setItem(key, value); } catch { /* ignore */ }
  },
  remove(key) {
    try { localStorage.removeItem(key); } catch { /* ignore */ }
  }
};

// ---------------------------------------------------------------- formatting

const pad = (n) => String(n).padStart(2, "0");

/** 00:07 or 1:02:03: what Xcode prints next to a running build. */
export function fmtClock(ms) {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return h ? `${h}:${pad(m)}:${pad(s)}` : `${pad(m)}:${pad(s)}`;
}

/** 850 ms, 1.2 s, 2 min 3 s */
export function fmtDuration(ms) {
  if (ms == null || Number.isNaN(ms)) return "";
  if (ms < 1000) return `${Math.round(ms)} ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(ms < 10_000 ? 1 : 0)} s`;
  const total = Math.round(ms / 1000);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return h ? `${h} h ${m} min` : `${m} min ${s} s`;
}

export function fmtSeconds(seconds) {
  if (seconds == null || Number.isNaN(seconds)) return "";
  if (seconds < 1) return `${seconds.toFixed(3)} s`;
  return fmtDuration(seconds * 1000);
}

export function fmtBytes(bytes) {
  if (bytes == null || Number.isNaN(bytes)) return "";
  if (bytes < 1024) return `${bytes} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit += 1; }
  return `${value >= 100 ? value.toFixed(0) : value.toFixed(1)} ${units[unit]}`;
}

const timeFmt = new Intl.DateTimeFormat(undefined, { hour: "numeric", minute: "2-digit" });
const timeSecFmt = new Intl.DateTimeFormat(undefined, { hour: "numeric", minute: "2-digit", second: "2-digit" });
const dateFmt = new Intl.DateTimeFormat(undefined, { year: "numeric", month: "short", day: "numeric" });
const shortDateFmt = new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric" });

export function dayStart(date = new Date()) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
}

export function fmtTime(iso) { return iso ? timeFmt.format(new Date(iso)) : ""; }
export function fmtTimeSec(iso) { return iso ? timeSecFmt.format(new Date(iso)) : ""; }

/** Today at 9:41 AM / Yesterday at 9:41 AM / Mar 3, 2026 at 9:41 AM */
export function fmtWhen(iso) {
  if (!iso) return "";
  const date = new Date(iso);
  const today = dayStart();
  const day = dayStart(date);
  if (day === today) return `Today at ${timeFmt.format(date)}`;
  if (day === today - 86_400_000) return `Yesterday at ${timeFmt.format(date)}`;
  return `${dateFmt.format(date)} at ${timeFmt.format(date)}`;
}

export function fmtDay(iso) {
  const date = new Date(iso);
  const today = dayStart();
  const day = dayStart(date);
  if (day === today) return "Today";
  if (day === today - 86_400_000) return "Yesterday";
  return dateFmt.format(date);
}

export function fmtShortDate(iso) { return shortDateFmt.format(new Date(iso)); }

/** 12 s ago, 3 min ago, 2 h ago, or a date. */
export function relTime(iso, now = Date.now()) {
  if (!iso) return "";
  const diff = Math.max(0, now - Date.parse(iso));
  if (diff < 5_000) return "just now";
  if (diff < 60_000) return `${Math.floor(diff / 1000)} s ago`;
  if (diff < 3_600_000) return `${Math.floor(diff / 60_000)} min ago`;
  if (diff < 86_400_000) return `${Math.floor(diff / 3_600_000)} h ago`;
  return fmtWhen(iso);
}

export function plural(n, one, many = `${one}s`) { return `${n} ${n === 1 ? one : many}`; }

export function shortId(id) { return String(id ?? "").slice(0, 8); }

// ---------------------------------------------------------------- fuzzy matching (Open Quickly, filters)

/**
 * Subsequence match with a score: consecutive characters, word starts and early matches score higher.
 * Returns null when `query` is not a subsequence of `text`, else { score, indices }.
 */
export function fuzzy(query, text) {
  const q = query.toLowerCase();
  const t = text.toLowerCase();
  if (!q) return { score: 0, indices: [] };
  const indices = [];
  let score = 0;
  let last = -2;
  let from = 0;
  for (let qi = 0; qi < q.length; qi += 1) {
    const at = t.indexOf(q[qi], from);
    if (at === -1) return null;
    indices.push(at);
    const boundary = at === 0 || /[\s._\-:/()[\]]/.test(t[at - 1]);
    score += 1 + (at === last + 1 ? 5 : 0) + (boundary ? 4 : 0) - Math.min(at, 30) * 0.05;
    last = at;
    from = at + 1;
  }
  if (t.includes(q)) score += 8;
  if (t.startsWith(q)) score += 6;
  return { score: score - text.length * 0.01, indices };
}

/** Wraps matched character indices in <mark>. Safe: everything is escaped. */
export function highlight(text, indices) {
  if (!indices || !indices.length) return raw(esc(text));
  const set = new Set(indices);
  let out = "";
  let open = false;
  for (let i = 0; i < text.length; i += 1) {
    const hit = set.has(i);
    if (hit && !open) { out += "<mark>"; open = true; }
    if (!hit && open) { out += "</mark>"; open = false; }
    out += esc(text[i]);
  }
  if (open) out += "</mark>";
  return raw(out);
}

/** Highlights case-insensitive substring occurrences of `needle`. */
export function highlightSubstring(text, needle) {
  if (!needle) return raw(esc(text));
  const lower = text.toLowerCase();
  const n = needle.toLowerCase();
  let out = "";
  let pos = 0;
  for (;;) {
    const at = lower.indexOf(n, pos);
    if (at === -1) break;
    out += `${esc(text.slice(pos, at))}<mark>${esc(text.slice(at, at + n.length))}</mark>`;
    pos = at + n.length;
  }
  return raw(out + esc(text.slice(pos)));
}

// ---------------------------------------------------------------- keyboard labels

const MAC_KEYS = { ctrl: "⌃", alt: "⌥", shift: "⇧", cmd: "⌘", enter: "↩", esc: "⎋", backspace: "⌫", up: "↑", down: "↓", left: "←", right: "→" };

/**
 * "mod+shift+o" -> "⇧⌘O" on Apple platforms, "Ctrl+Shift+O" elsewhere. `mod` is Cmd on Mac and Ctrl elsewhere
 * (the same rule the key handler applies).
 */
export function keyLabel(combo) {
  const parts = combo.split("+");
  if (isMac) {
    const order = ["ctrl", "alt", "shift", "mod"];
    const mods = order.filter((m) => parts.includes(m)).map((m) => (m === "mod" ? MAC_KEYS.cmd : MAC_KEYS[m]));
    const key = parts.find((p) => !order.includes(p));
    return mods.join("") + (MAC_KEYS[key] ?? (key.length === 1 ? key.toUpperCase() : key[0].toUpperCase() + key.slice(1)));
  }
  const names = parts.map((p) => {
    if (p === "mod") return "Ctrl";
    if (p === "alt") return "Alt";
    if (p === "enter") return "Enter";
    if (p === "esc") return "Esc";
    return p.length === 1 ? p.toUpperCase() : p[0].toUpperCase() + p.slice(1);
  });
  const order = ["Ctrl", "Alt", "Shift"];
  const mods = order.filter((m) => names.includes(m));
  const key = names.filter((n) => !order.includes(n));
  return [...mods, ...key].join("+");
}

export async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.className = "sr-only";
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try { ok = document.execCommand("copy"); } catch { ok = false; }
    area.remove();
    return ok;
  }
}

export function groupBy(items, keyOf) {
  const map = new Map();
  for (const item of items) {
    const key = keyOf(item);
    if (!map.has(key)) map.set(key, []);
    map.get(key).push(item);
  }
  return map;
}

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
