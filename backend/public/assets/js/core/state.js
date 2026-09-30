// Application state, persisted preferences and the render scheduler.
// Data modules mutate `state` and call `invalidate(area...)`; each area registers one renderer. Renderers are pure
// functions of `state` that end in `patch()` (see morph.js), so re-rendering is cheap when nothing changed.

import { debounce, storage } from "./util.js";

const PREF_KEY = "mobilelab.prefs.v1";

const defaults = {
  navW: 300,
  inspW: 300,
  debugH: 240,
  navOpen: true,
  inspOpen: true,
  debugOpen: true,
  navTab: "devices",
  inspTab: "attributes",
  theme: "system",
  glass: "on", // "on" | "reduced" (Settings: Liquid Glass)
  expanded: {},
  collapsedSections: {},
  scheme: "",
  schemes: [],
  destinations: [],
  reportTab: "summary",
  debugLayout: "both",
  varsMode: "auto",
  consoleMode: "all",
  onlyBooted: false,
  recentOnly: { tests: false, reports: false },
  failedOnly: { tests: false, reports: false }
};

export const prefs = { ...defaults, ...storage.get(PREF_KEY, {}) };
for (const key of Object.keys(defaults)) if (prefs[key] === undefined) prefs[key] = defaults[key];

export const savePrefs = debounce(() => storage.set(PREF_KEY, prefs), 150, 1000);

export function setPref(key, value) {
  prefs[key] = value;
  savePrefs();
}

export const state = {
  conn: "connecting", // connecting | online | offline
  connError: "",
  auth: "unknown", // unknown | ok | required
  health: null,
  caps: null,
  catalog: null,
  catalogError: "",
  doctor: null,
  doctorError: "",
  doctorLoading: false,

  devices: [],
  capacity: null,
  jobs: [],
  runs: [],
  loaded: { devices: false, jobs: false, runs: false },
  errors: {},
  events: [],
  metrics: null,
  metricsHistory: [],

  /** Lazy per-job data: jobId -> { status: "loading" | "ok" | "error", data, error } */
  results: new Map(),
  artifacts: new Map(),
  jobEvents: new Map(),

  /** What the editor shows. */
  sel: { kind: "welcome", id: null },
  history: { stack: [{ kind: "welcome", id: null }], index: 0 },
  reveal: null,
  eventHistory: null,
  catalogAt: 0,

  /** Transient UI state that is not worth persisting. */
  ui: {
    filter: { devices: "", tests: "", issues: "", reports: "", find: "" },
    findScope: "devices",
    focusNav: false,
    busy: new Set(),
    treeFocus: {},
    logFilter: "",
    consoleFilter: "",
    varsFilter: "",
    narrowScreen: "nav", // nav | editor (narrow layout only)
    narrowSheet: null, // "inspector" | "debug" while open as a sheet (narrow layout only)
    sheet: null,
    narrow: false,
    demoCatalogLoading: false
  }
};

// ---------------------------------------------------------------- render scheduling

const renderers = new Map();
const dirty = new Set();
let scheduled = false;

export function onRender(area, fn) {
  renderers.set(area, fn);
}

function flush() {
  scheduled = false;
  const areas = [...dirty];
  dirty.clear();
  for (const area of areas) {
    const fn = renderers.get(area);
    if (!fn) continue;
    try { fn(); } catch (error) { console.error(`render(${area}) failed`, error); }
  }
}

export function invalidate(...areas) {
  for (const area of areas.length ? areas : ["all"]) {
    if (area === "all") for (const key of renderers.keys()) dirty.add(key);
    else dirty.add(area);
  }
  if (!scheduled) {
    scheduled = true;
    requestAnimationFrame(flush);
  }
}

/** Renders synchronously (tests, and code that needs the DOM right now). */
export function flushNow() {
  flush();
}

// ---------------------------------------------------------------- selection and history

export function sameSel(a, b) {
  return a.kind === b.kind && (a.id ?? null) === (b.id ?? null);
}

export function select(kind, id = null, { record = true, reveal = null } = {}) {
  const next = { kind, id };
  const changed = !sameSel(state.sel, next);
  state.sel = next;
  state.reveal = reveal ? { ...reveal, nonce: Date.now() + Math.random() } : null;
  if (changed && record) {
    const h = state.history;
    h.stack = h.stack.slice(0, h.index + 1);
    h.stack.push(next);
    if (h.stack.length > 60) h.stack.shift();
    h.index = h.stack.length - 1;
  }
  if (state.ui.narrow && kind !== "welcome") state.ui.narrowScreen = "editor";
  invalidate("shell", "nav", "jump", "editor", "inspector", "debug", "toolbar");
}

export function historyGo(delta) {
  const h = state.history;
  const index = h.index + delta;
  if (index < 0 || index >= h.stack.length) return;
  h.index = index;
  const target = h.stack[index];
  select(target.kind, target.id, { record: false });
}

export const canGoBack = () => state.history.index > 0;
export const canGoForward = () => state.history.index < state.history.stack.length - 1;

// ---------------------------------------------------------------- lookups

export const deviceById = (id) => state.devices.find((d) => d.id === id);
export const jobById = (id) => state.jobs.find((j) => j.id === id);
export const runById = (id) => state.runs.find((r) => r.id === id);

export const ACTIVE = ["queued", "running", "retrying"];
export const isActive = (job) => ACTIVE.includes(job.status);
export const isTerminal = (job) => !isActive(job);

/** Tree expansion is stored per navigator as { rowKey: boolean }; a row's own default applies until the user decides. */
export function isExpanded(nav, key, fallback = false) {
  const map = prefs.expanded[nav];
  return map && Object.prototype.hasOwnProperty.call(map, key) ? !!map[key] : fallback;
}

export function setExpanded(nav, key, open) {
  if (!prefs.expanded[nav] || Array.isArray(prefs.expanded[nav])) prefs.expanded[nav] = {};
  prefs.expanded[nav][key] = !!open;
  savePrefs();
}
