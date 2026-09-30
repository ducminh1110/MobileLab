// Window chrome: layout state (panel visibility, persisted sizes, draggable dividers), the narrow single-pane
// layout, appearance, and the global keyboard shortcuts.

import { installSprite } from "../core/icons.js";
import { state, prefs, setPref, onRender, invalidate } from "../core/state.js";
import { clamp } from "../core/util.js";
import { commandForEvent, hooks, isEnabled, regions, togglePanel } from "../core/actions.js";
import { glassBusy } from "../core/glass.js";
import { action, onKey } from "./dispatch.js";

const LIMITS = { nav: [220, 460, 300], insp: [240, 460, 300], debug: [120, 640, 240] };
const app = () => document.getElementById("app");

export function applyTheme() {
  const root = document.documentElement;
  if (prefs.theme === "light" || prefs.theme === "dark") root.dataset.theme = prefs.theme;
  else delete root.dataset.theme;
}

function effectiveWidth(kind) {
  const [min, max] = LIMITS[kind];
  const pref = kind === "nav" ? prefs.navW : prefs.inspW;
  const cap = Math.floor(window.innerWidth * 0.32);
  return clamp(Math.min(pref, Math.max(min, cap)), min, max);
}

function applyLayout() {
  const el = app();
  const narrow = state.ui.narrow;
  el.dataset.layout = narrow ? "narrow" : "wide";
  el.dataset.screen = narrow ? state.ui.narrowScreen : "wide";
  el.dataset.nav = narrow || prefs.navOpen ? "open" : "closed";
  el.dataset.insp = !narrow && prefs.inspOpen ? "open" : "closed";
  el.dataset.debug = !narrow && prefs.debugOpen ? "open" : "closed";
  el.dataset.sheet = narrow && state.ui.narrowSheet ? state.ui.narrowSheet : "";
  document.getElementById("navigator")?.toggleAttribute("inert", !narrow && el.dataset.nav !== "open");
  document.getElementById("inspector")?.toggleAttribute("inert", narrow ? state.ui.narrowSheet !== "inspector" : el.dataset.insp !== "open");
  el.style.setProperty("--nav-w", `${effectiveWidth("nav")}px`);
  el.style.setProperty("--insp-w", `${effectiveWidth("insp")}px`);
  el.style.setProperty("--debug-h", `${clamp(prefs.debugH, LIMITS.debug[0], Math.max(LIMITS.debug[0], window.innerHeight - 260))}px`);
  syncDividerAria();
}

function syncDividerAria() {
  const set = (id, now, [min, max]) => {
    const d = document.getElementById(id);
    d.setAttribute("aria-valuenow", String(Math.round(now)));
    d.setAttribute("aria-valuemin", String(min));
    d.setAttribute("aria-valuemax", String(max));
  };
  set("div-nav", effectiveWidth("nav"), LIMITS.nav);
  set("div-insp", effectiveWidth("insp"), LIMITS.insp);
  set("div-debug", prefs.debugH, LIMITS.debug);
}

onRender("shell", applyLayout);

// ---------------------------------------------------------------- dividers

const KINDS = {
  nav: { size: "navW", open: "navOpen", panel: "navigator", grow: +1, vertical: true, snap: 56 },
  insp: { size: "inspW", open: "inspOpen", panel: "inspector", grow: -1, vertical: true, snap: 56 },
  debug: { size: "debugH", open: "debugOpen", panel: "debug", grow: -1, vertical: false, snap: 44 }
};

const sizeNow = (kind) => (kind === "nav" ? effectiveWidth("nav") : kind === "insp" ? effectiveWidth("insp") : prefs.debugH);

/** Called around a drag that passes the minimum: the panel shows as collapsed (animated) and commits on release. */
function previewCollapse(kind, collapsed) {
  const el = app();
  const attr = { nav: "nav", insp: "insp", debug: "debug" }[kind];
  el.dataset[attr] = collapsed ? "closed" : "open";
  el.classList.add("snapping");
  clearTimeout(previewCollapse.timer);
  previewCollapse.timer = setTimeout(() => el.classList.remove("snapping"), 300);
}

function initDivider(id, kind) {
  const el = document.getElementById(id);
  const cfg = KINDS[kind];
  const { vertical } = cfg;
  const [min, max, dflt] = LIMITS[kind];
  const write = (value) => {
    const hi = kind === "debug" ? Math.max(min, Math.min(max, window.innerHeight - 260)) : max;
    setPref(cfg.size, Math.round(clamp(value, min, hi)));
    applyLayout();
  };

  el.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    event.preventDefault();
    el.setPointerCapture?.(event.pointerId);
    const start = vertical ? event.clientX : event.clientY;
    const initial = sizeNow(kind);
    let collapsed = false;
    el.classList.add("dragging");
    app().classList.add("dragging", vertical ? "drag-v" : "drag-h");
    glassBusy(true);
    const move = (e) => {
      const delta = (vertical ? e.clientX : e.clientY) - start;
      const raw = initial + cfg.grow * delta;
      const wantCollapse = raw < min - cfg.snap;
      if (wantCollapse !== collapsed) { collapsed = wantCollapse; previewCollapse(kind, collapsed); }
      if (!collapsed) write(raw);
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      window.removeEventListener("pointercancel", up);
      el.classList.remove("dragging");
      app().classList.remove("dragging", "drag-v", "drag-h");
      glassBusy(false);
      if (collapsed) togglePanel(cfg.panel, false);
      else applyLayout();
      window.dispatchEvent(new Event("mobilelab:resized"));
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    window.addEventListener("pointercancel", up);
  });

  // double click: collapse (Xcode toggles the panel; the toolbar buttons and shortcuts bring it back)
  el.addEventListener("dblclick", (event) => { event.preventDefault(); togglePanel(cfg.panel, false); });

  el.addEventListener("keydown", (event) => {
    const step = event.shiftKey ? 40 : 10;
    const growKey = vertical ? (cfg.grow > 0 ? "ArrowRight" : "ArrowLeft") : "ArrowUp";
    const shrinkKey = vertical ? (cfg.grow > 0 ? "ArrowLeft" : "ArrowRight") : "ArrowDown";
    const now = sizeNow(kind);
    if (event.key === growKey) { event.preventDefault(); write(now + step); }
    else if (event.key === shrinkKey) { event.preventDefault(); write(now - step); }
    else if (event.key === "Home") { event.preventDefault(); write(min); }
    else if (event.key === "End") { event.preventDefault(); write(max); }
    else if (event.key === "Enter") { event.preventDefault(); togglePanel(cfg.panel, false); }
    else if (event.key === "0" && !event.ctrlKey && !event.metaKey) { event.preventDefault(); write(dflt); }
    else return;
    window.dispatchEvent(new Event("mobilelab:resized"));
  });
}

// ---------------------------------------------------------------- focus follows the layout

const PANEL_EL = { navigator: "navigator", inspector: "inspector", debug: "debug" };

/** Before a panel collapses: if the keyboard focus is inside it, move focus to the editor so it is never lost to <body>. */
function leavePanel(name) {
  const panel = document.getElementById(PANEL_EL[name]);
  if (panel && panel.contains(document.activeElement)) document.getElementById("editor")?.focus({ preventScroll: true });
}

/** After a panel opens from the keyboard: focus goes into it (the selected tab, the first tree row, the first control). */
function enterPanel(name) {
  const go = () => {
    const target =
      name === "navigator" ? document.querySelector('#nav-tabs [role="tab"][aria-selected="true"]')
      : name === "inspector" ? document.querySelector('#inspector [role="tab"][aria-selected="true"]')
      : document.querySelector('#debug .vars [role="treeitem"][tabindex="0"], #debug .debug-bar button:not(:disabled)');
    target?.focus({ preventScroll: true });
  };
  requestAnimationFrame(() => requestAnimationFrame(go));
}

// ---------------------------------------------------------------- init

const isTyping = (target) => {
  const el = target;
  if (!el || !el.tagName) return false;
  return el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.tagName === "SELECT" || el.isContentEditable;
};

export function initShell() {
  installSprite(document.getElementById("sprite"));
  const overlay = document.getElementById("overlay");
  overlay.innerHTML = '<div id="sheet-layer"></div><div id="menu-layer"></div>';
  applyTheme();

  const query = window.matchMedia("(max-width: 899px)");
  const sync = () => {
    state.ui.narrow = query.matches;
    if (!query.matches) { state.ui.narrowSheet = null; }
    invalidate("all");
  };
  query.addEventListener("change", sync);
  state.ui.narrow = query.matches;

  initDivider("div-nav", "nav");
  initDivider("div-insp", "insp");
  initDivider("div-debug", "debug");
  window.addEventListener("resize", () => { invalidate("shell"); window.dispatchEvent(new Event("mobilelab:resized")); });

  action("close-sheet", () => { state.ui.narrowSheet = null; invalidate("shell", "toolbar"); });

  onKey((event) => {
    if (event.defaultPrevented || event.isComposing) return false;
    if (state.ui.sheet) return false; // a sheet is modal; it handles its own keys
    const cmd = commandForEvent(event, isTyping(event.target));
    if (!cmd) return false;
    event.preventDefault();
    event.stopPropagation();
    if (isEnabled(cmd)) cmd.run();
    return true;
  });

  hooks.panelClosing = leavePanel;
  hooks.panelOpened = enterPanel;
  regions.set("filter", () => {
    requestAnimationFrame(() => (document.querySelector(".find-input") || document.querySelector(".nf-input"))?.focus());
  });
}
