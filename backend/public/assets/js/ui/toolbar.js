// The toolbar: navigator toggle, Stop, Run, the title (application menu), the capsule (scheme popup,
// destination popup with multi-select and on-demand entries, status area) and the debug / inspector toggles.

import { html, keyLabel, plural, raw } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, onRender, invalidate } from "../core/state.js";
import {
  activeJobs, bootedSimulators, capsuleStatus, currentScheme, destinationInfo, destinationLabel, destinationIcon, effectiveDestinations,
  runPlan, schemeNames, modelLabel, runtimeLabel
} from "../core/models.js";
import { ensureCatalog } from "../core/sync.js";
import { chooseScheme, commandById, hooks, isEnabled, openJob, runScheme, setDestinations, showNavigator, stopAll, togglePanel } from "../core/actions.js";
import { action } from "./dispatch.js";
import { openMenu } from "./menu.js";
import { elapsedSpan } from "./ticker.js";

const MAX_ON_DEMAND = 24;

function statusView() {
  const err = state.ui.runError;
  if (err && Date.now() - err.at < 8000) return { kind: "fail", label: "Run Failed", detail: err.message };
  return capsuleStatus();
}

function statusGlyphFor(kind) {
  switch (kind) {
    case "running": return html`<span class="spin spin-sm c-accent" role="img" aria-label="Running"></span>`;
    case "retrying": return html`<span class="spin spin-sm c-warn" role="img" aria-label="Retrying"></span>`;
    case "pass": return icon("checkmark.diamond.fill", "ic-14 c-pass");
    case "fail": return icon("xmark.diamond.fill", "ic-14 c-fail");
    case "offline": return icon("bolt.horizontal", "ic-14 c-fail");
    case "warn": return icon("exclamationmark.triangle.fill", "ic-14 c-warn");
    case "queued": return icon("clock", "ic-14 c-secondary");
    case "cancelled": return icon("minus.diamond", "ic-14 c-dim");
    default: return "";
  }
}

function toolbarHtml() {
  const st = statusView();
  const scheme = currentScheme();
  const active = activeJobs().length;
  const demo = (state.health?.mode || state.caps?.mode) === "demo";
  const online = state.conn === "online";
  const plan = runPlan();
  const narrowEditor = state.ui.narrow && state.ui.narrowScreen === "editor";
  const runTitle = scheme ? `Run ${scheme} on ${destinationLabel()}${plan.matrix ? ` (${plural(plan.jobs, "job")})` : ""} (${keyLabel("mod+enter")})` : `Run… choose a scheme first (${keyLabel("mod+enter")})`;
  const detail = st.elapsedFrom
    ? html`${st.detail}, ${elapsedSpan(st.elapsedFrom)}${st.extra ? html`<span class="st-more">${st.extra}</span>` : ""}`
    : html`${st.detail}${st.extra ? html`<span class="st-more">${st.extra}</span>` : ""}`;

  return html`
    <div class="tb-left">
      ${narrowEditor
        ? html`<button type="button" class="icon-btn tb-btn tb-back" data-action="narrow-back" title="Back to the navigator" aria-label="Back to the navigator">${icon("chevron.left", "ic-18")}</button>`
        : html`<button type="button" class="icon-btn tb-btn tb-nav wide-only" data-action="toggle-navigator" aria-pressed="${prefs.navOpen ? "true" : "false"}" title="${prefs.navOpen ? "Hide" : "Show"} Navigator (${keyLabel("mod+shift+0")})" aria-label="${prefs.navOpen ? "Hide" : "Show"} navigator">${icon("sidebar.left", "ic-18")}</button>`}
      <span class="tb-run">
        <button type="button" class="icon-btn tb-btn tb-stop" data-action="stop" ${active ? "" : raw("disabled")} title="Stop${active ? ` (${plural(active, "active job")})` : ""} (${keyLabel("mod+.")})" aria-label="Stop">${icon("stop.fill", "ic-18")}</button>
        <button type="button" class="icon-btn tb-btn tb-play" data-action="run" ${online ? "" : raw("disabled")} title="${runTitle}" aria-label="Run">${icon("play.fill", "ic-18")}</button>
      </span>
    </div>
    <div class="tb-title">
      <button type="button" class="app-menu" data-action="app-menu" aria-haspopup="menu" aria-label="MobileLab menu" title="MobileLab menu">${icon("mark", "ic-18")}<span>MobileLab</span></button>
    </div>
    <div class="capsule" role="group" aria-label="Scheme, destination and status">
      <button type="button" class="cap-seg cap-scheme" data-action="scheme-menu" aria-haspopup="menu" aria-expanded="false" title="Scheme: ${scheme || "none"}">${icon("mark", "ic-16 ic-mark")}<span>${scheme || "No Scheme"}</span></button>
      <span class="cap-chev">${icon("chevron.right", "ic-10")}</span>
      <button type="button" class="cap-seg cap-dest" data-action="dest-menu" aria-haspopup="menu" aria-expanded="false" title="Destination: ${destinationLabel()}">${icon(destinationIcon(), "ic-14")}<span>${destinationLabel()}</span></button>
      <button type="button" class="cap-status" data-action="open-status" data-kind="${st.kind}" ${st.jobId ? raw(`data-id="${st.jobId}"`) : ""} aria-live="polite" title="${st.label}${st.detail ? " | " + st.detail : ""}">
        ${demo ? html`<span class="tag tag-demo" title="Demo mode: simulators and test results are simulated">Demo</span>` : ""}
        ${statusGlyphFor(st.kind)}
        <span class="st-label">${st.label}</span>
        ${st.detail || st.elapsedFrom ? html`<span class="st-sep">|</span><span class="st-detail">${detail}</span>` : ""}
      </button>
    </div>
    <div class="tb-right">
      <button type="button" class="icon-btn" data-action="toggle-debug" aria-pressed="${prefs.debugOpen ? "true" : "false"}" title="${prefs.debugOpen ? "Hide" : "Show"} Debug Area (${keyLabel("mod+shift+y")})" aria-label="${prefs.debugOpen ? "Hide" : "Show"} debug area">${icon("sidebar.bottom", "ic-18")}</button>
      <button type="button" class="icon-btn" data-action="toggle-inspector" aria-pressed="${prefs.inspOpen ? "true" : "false"}" title="${prefs.inspOpen ? "Hide" : "Show"} Inspector (${keyLabel("mod+alt+0")})" aria-label="${prefs.inspOpen ? "Hide" : "Show"} inspector">${icon("sidebar.right", "ic-18")}</button>
    </div>`;
}

onRender("toolbar", () => patch(document.getElementById("toolbar"), toolbarHtml()));

// ---------------------------------------------------------------- menus

function appMenuItems() {
  const item = (id, extra = {}) => {
    const c = commandById(id);
    return { label: c.title, keys: c.keys, disabled: !isEnabled(c), run: () => c.run(), ...extra };
  };
  return [
    item("new-simulator", { icon: "iphone" }),
    item("new-scheme"),
    item("edit-scheme"),
    { separator: true },
    item("run", { icon: "play.fill" }),
    item("stop", { icon: "stop.fill" }),
    { separator: true },
    item("open-quickly", { icon: "magnifyingglass" }),
    item("find-navigator"),
    { separator: true },
    { header: "View" },
    item("toggle-navigator"),
    item("toggle-inspector"),
    item("toggle-debug"),
    { separator: true },
    item("diagnostics", { icon: "stethoscope" }),
    item("sync-devices", { icon: "arrow.clockwise" }),
    item("settings", { icon: "gearshape" }),
    item("shortcuts", { icon: "questionmark.circle" })
  ];
}

function schemeMenuItems() {
  const current = currentScheme();
  const names = schemeNames();
  const items = [];
  if (names.length) {
    items.push({ header: "Schemes" });
    for (const name of names) items.push({ label: name, checked: name === current, run: () => chooseScheme(name) });
    items.push({ separator: true });
  }
  items.push({ label: "New Scheme…", run: () => hooks.sheet("scheme", { isNew: true }) });
  items.push({ label: "Edit Scheme…", disabled: !current, run: () => hooks.sheet("scheme", {}) });
  return items;
}

function destinationMenuItems() {
  const selected = new Set(effectiveDestinations());
  const items = [];
  const toggle = (key) => () => {
    const keys = new Set(effectiveDestinations());
    if (key === "auto") { keys.clear(); keys.add("auto"); }
    else {
      keys.delete("auto");
      if (keys.has(key)) keys.delete(key); else keys.add(key);
    }
    if (!keys.size) keys.add("auto");
    setDestinations([...keys]);
    handle?.update(destinationMenuItems());
  };
  items.push({ label: "Any Available Simulator", icon: "iphone", checked: selected.has("auto"), keepOpen: true, run: toggle("auto") });

  const booted = bootedSimulators();
  items.push({ header: "Booted Simulators" });
  if (!booted.length) items.push({ label: "No simulators booted", disabled: true });
  for (const d of booted) {
    const info = destinationInfo(`dev:${d.id}`);
    items.push({ label: d.name, icon: info.icon, sub: d.runtimeName || runtimeLabel(d.runtime), checked: selected.has(`dev:${d.id}`), keepOpen: true, run: toggle(`dev:${d.id}`) });
  }

  items.push({ header: "Create on Demand" });
  const cat = state.catalog;
  if (!cat) items.push({ label: state.catalogError ? `Catalog unavailable: ${state.catalogError}` : "Loading…", disabled: true });
  else {
    const list = [];
    for (const rt of cat.runtimes || []) for (const ty of cat.deviceTypes || []) list.push({ rt, ty });
    for (const { rt, ty } of list.slice(0, MAX_ON_DEMAND)) {
      const key = `new:${rt.identifier}|${ty.identifier}`;
      items.push({ label: `${ty.name || modelLabel(ty.identifier)}`, sub: rt.name, icon: /ipad/i.test(ty.name) ? "ipad" : "iphone", checked: selected.has(key), keepOpen: true, run: toggle(key) });
    }
    if (list.length > MAX_ON_DEMAND) items.push({ label: `${list.length - MAX_ON_DEMAND} more in the create-simulator sheet`, disabled: true });
    if (!list.length) items.push({ label: "The host reports no runtimes or device types", disabled: true });
  }

  const plan = runPlan();
  if (plan.matrix) {
    items.push({ separator: true });
    items.push({ label: `Matrix run: ${plural(plan.runtimes.length || 1, "runtime")} × ${plural(plan.models.length || 1, "device type")} = ${plural(plan.jobs, "job")}`, disabled: true });
  }
  items.push({ separator: true });
  items.push({ label: "Manage Devices…", run: () => showNavigator("devices") });
  return items;
}

let handle = null;

export function initToolbar() {
  action("toggle-navigator", () => togglePanel("navigator"));
  action("toggle-debug", () => togglePanel("debug"));
  action("toggle-inspector", () => togglePanel("inspector"));
  action("run", () => runScheme());
  action("stop", () => stopAll());
  action("narrow-back", () => { state.ui.narrowScreen = "nav"; invalidate("shell", "toolbar"); });
  action("open-status", ({ el }) => {
    if (el.dataset.id) openJob(el.dataset.id);
  });
  action("app-menu", ({ el }) => openMenu({ anchor: el, items: appMenuItems(), label: "MobileLab menu", minWidth: 250 }));
  action("scheme-menu", ({ el }) => { handle = openMenu({ anchor: el, items: schemeMenuItems(), label: "Scheme", minWidth: 220, onClose: () => { handle = null; } }); });
  action("dest-menu", ({ el }) => {
    handle = openMenu({ anchor: el, items: destinationMenuItems(), label: "Destination", minWidth: 280, onClose: () => { handle = null; } });
    void ensureCatalog().then(() => handle?.update(destinationMenuItems()));
  });
}
