// Window chrome: layout state (panel visibility, persisted sizes, draggable dividers), the narrow single-pane
// layout, appearance, and the global keyboard shortcuts.

import { installSprite } from "../core/icons.js";
import { state, prefs, setPref, onRender, invalidate } from "../core/state.js";
import { clamp } from "../core/util.js";
import { commandForEvent, isEnabled, regions } from "../core/actions.js";
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

function initDivider(id, kind) {
  const el = document.getElementById(id);
  const vertical = kind !== "debug";
  const read = () => (kind === "nav" ? prefs.navW : kind === "insp" ? prefs.inspW : prefs.debugH);
  const write = (value) => {
    const [min, max] = LIMITS[kind];
    const next = kind === "debug" ? clamp(value, min, Math.max(min, Math.min(max, window.innerHeight - 260))) : clamp(value, min, max);
    setPref(kind === "nav" ? "navW" : kind === "insp" ? "inspW" : "debugH", Math.round(next));
    applyLayout();
  };

  el.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    event.preventDefault();
    el.setPointerCapture(event.pointerId);
    const start = vertical ? event.clientX : event.clientY;
    const initial = kind === "nav" ? effectiveWidth("nav") : kind === "insp" ? effectiveWidth("insp") : read();
    el.classList.add("dragging");
    app().classList.add("dragging", vertical ? "drag-v" : "drag-h");
    const move = (e) => {
      const delta = (vertical ? e.clientX : e.clientY) - start;
      write(kind === "nav" ? initial + delta : initial - delta);
    };
    const up = () => {
      el.removeEventListener("pointermove", move);
      el.removeEventListener("pointerup", up);
      el.removeEventListener("pointercancel", up);
      el.classList.remove("dragging");
      app().classList.remove("dragging", "drag-v", "drag-h");
      window.dispatchEvent(new Event("mobilelab:resized"));
    };
    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  });

  el.addEventListener("dblclick", () => write(LIMITS[kind][2]));

  el.addEventListener("keydown", (event) => {
    const step = event.shiftKey ? 40 : 10;
    const grow = vertical ? (kind === "nav" ? "ArrowRight" : "ArrowLeft") : "ArrowUp";
    const shrink = vertical ? (kind === "nav" ? "ArrowLeft" : "ArrowRight") : "ArrowDown";
    const now = kind === "nav" ? effectiveWidth("nav") : kind === "insp" ? effectiveWidth("insp") : read();
    if (event.key === grow) { event.preventDefault(); write(now + step); }
    else if (event.key === shrink) { event.preventDefault(); write(now - step); }
    else if (event.key === "Home") { event.preventDefault(); write(LIMITS[kind][0]); }
    else if (event.key === "End") { event.preventDefault(); write(LIMITS[kind][1]); }
    else if (event.key === "Enter") { event.preventDefault(); write(LIMITS[kind][2]); }
    else return;
    window.dispatchEvent(new Event("mobilelab:resized"));
  });
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

  regions.set("filter", () => {
    requestAnimationFrame(() => (document.querySelector(".find-input") || document.querySelector(".nf-input"))?.focus());
  });
}
