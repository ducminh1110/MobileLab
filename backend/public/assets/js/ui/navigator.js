// The navigator column: icon tab bar (blue selected circle), the active navigator's tree, and the filter bar.
// Also owns what happens when a row is opened, and the device / job / run context menus.

import { html, keyLabel } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, setPref, select, invalidate, onRender, deviceById, jobById, runById, isActive } from "../core/state.js";
import { NAV_TABS, showNavigator, openJob, hooks, bootDevice, shutdownDevice, screenshotDevice, runOnDevice, rerunJob, cancelJob, cancelRun, exportJUnit, retryFailed, copy, runScheme, confirmDeleteDevice } from "../core/actions.js";
import { setMetricsPolling, resultsOf, loadAll } from "../core/sync.js";
import { isBooted, isTransitioning, jobTitle, runName } from "../core/models.js";
import { action, field, contextMenu } from "./dispatch.js";
import { openMenu } from "./menu.js";
import { initTreeKeys, toggleRow } from "./tree.js";
import { devicesView } from "./nav-devices.js";
import { testsView, issuesView, reportsView } from "./nav-tests.js";
import { findView, FIND_SCOPES } from "./nav-find.js";
import { debugView } from "./nav-debug.js";

const VIEWS = { devices: devicesView, tests: testsView, issues: issuesView, find: findView, debug: debugView, reports: reportsView };

const FILTERS = {
  devices: { add: { label: "New Simulator…", action: "new-simulator" }, toggles: [{ id: "booted", icon: "bolt", label: "Show only booted simulators" }] },
  tests: { add: { label: "Run Tests", action: "run" }, toggles: [{ id: "recent", icon: "clock", label: "Show only the last 24 hours" }, { id: "failed", icon: "xmark.diamond.fill", label: "Show only failed runs" }] },
  issues: { toggles: [] },
  find: { placeholder: "Filter results", toggles: [] },
  debug: { toggles: [] },
  reports: { toggles: [{ id: "recent", icon: "clock", label: "Show only the last 24 hours" }, { id: "failed", icon: "xmark.diamond.fill", label: "Show only failed reports" }] }
};

const FILTER_KEY = { devices: "devices", tests: "tests", issues: "issues", find: "findResults", debug: "debug", reports: "reports" };

const scrollTops = {};
let lastNav = null;

const tabTitle = (t) => `${t.label} (${keyLabel(`mod+shift+${t.key}`)})`;

function tabsHtml(idPrefix) {
  return html`${NAV_TABS.map((t) => {
    const selected = prefs.navTab === t.id;
    return html`<button type="button" class="nt${selected ? " selected" : ""}" role="tab" id="${idPrefix}-${t.id}" data-action="nav-tab" data-arg="${t.id}"
      aria-selected="${selected ? "true" : "false"}" aria-controls="nav-body" tabindex="${selected ? 0 : -1}" title="${tabTitle(t)}" aria-label="${t.label}">${icon(t.icon, "ic-16")}</button>`;
  })}`;
}

function filterHtml() {
  const nav = prefs.navTab;
  const cfg = FILTERS[nav];
  const key = FILTER_KEY[nav];
  const value = state.ui.filter[key] ?? "";
  const pressed = (id) => (id === "booted" ? prefs.onlyBooted : id === "recent" ? prefs.recentOnly[nav] : prefs.failedOnly[nav]);
  return html`<div class="nf">
    ${cfg.add ? html`<button type="button" class="nf-add" data-action="nav-add" title="${cfg.add.label}" aria-label="${cfg.add.label}">${icon("plus", "ic-14")}</button>` : html`<span class="nf-add-space"></span>`}
    <label class="nf-field">
      <span class="nf-icon">${icon("line.3.horizontal.decrease.circle", "ic-14")}</span>
      <input type="text" class="nf-input" data-key="nf-input" data-scope="filter" data-field="filter" value="${value}" placeholder="${cfg.placeholder || "Filter"}" aria-label="Filter ${nav}" autocomplete="off" spellcheck="false">
      ${value ? html`<button type="button" class="nf-clear" data-action="filter-clear" aria-label="Clear filter" title="Clear">${icon("xmark.circle.fill", "ic-12")}</button>` : ""}
      ${cfg.toggles.map((t) => html`<button type="button" class="nf-toggle" data-action="nav-toggle" data-arg="${t.id}" aria-pressed="${pressed(t.id) ? "true" : "false"}" title="${t.label}" aria-label="${t.label}">${icon(t.icon, "ic-12")}</button>`)}
    </label>
  </div>`;
}

function renderNav() {
  const nav = prefs.navTab;
  const tabs = document.getElementById("nav-tabs");
  const body = document.getElementById("nav-body");
  const filter = document.getElementById("nav-filter");
  patch(tabs, tabsHtml("tab"));
  patch(document.getElementById("bottom-tabs"), tabsHtml("btab"));

  setMetricsPolling(nav === "debug" && prefs.navOpen && state.conn === "online");

  if (lastNav && lastNav !== nav) scrollTops[lastNav] = body.scrollTop;
  const view = VIEWS[nav]();
  body.setAttribute("role", "tabpanel");
  body.setAttribute("aria-labelledby", `tab-${nav}`);
  body.dataset.nav = nav;
  patch(body, view.html);
  if (lastNav !== nav) {
    body.scrollTop = scrollTops[nav] || 0;
    lastNav = nav;
  }
  patch(filter, filterHtml());
  filter.hidden = false;
}

onRender("nav", renderNav);

// ---------------------------------------------------------------- opening rows

export function openRow(row, { focusEditor = false } = {}) {
  const d = row.dataset;
  if (d.kind === "device") select("device", d.id);
  else if (d.kind === "run") select("run", d.id);
  else if (d.kind === "job") {
    openJob(d.id, { tab: d.tab, caseName: d.caseName, line: d.line ? Number(d.line) : undefined, query: d.query });
  } else return;
  if (focusEditor) requestAnimationFrame(() => document.getElementById("editor")?.focus());
}

function retryLoad() {
  void loadAll();
}

export function initNavigator() {
  initTreeKeys(openRow, toggleRow);

  action("nav-tab", ({ arg }) => showNavigator(arg));
  action("retry-load", retryLoad);
  action("nav-add", () => {
    const cfg = FILTERS[prefs.navTab].add;
    if (cfg) hooks.sheet(cfg.action === "new-simulator" ? "simulator" : "run");
  });
  action("row", ({ el, event }) => {
    if (event?.target?.closest?.('[data-action="row-toggle"], [data-action="rerun-job"], [data-action="backend-info"]')) return;
    const d = el.dataset;
    state.ui.treeFocus[d.nav] = d.rowKey;
    if (d.kind) openRow(el);
    else if (el.hasAttribute("data-expandable")) toggleRow(d.nav, d.rowKey, el.getAttribute("aria-expanded") !== "true");
    else if (state.ui.narrow) invalidate();
  });
  action("row-toggle", ({ el, event }) => {
    event?.stopPropagation();
    const row = el.closest('[role="treeitem"]');
    toggleRow(row.dataset.nav, row.dataset.rowKey, row.getAttribute("aria-expanded") !== "true");
  });
  action("rerun-job", ({ el, event }) => { event?.stopPropagation(); void rerunJob(el.dataset.id); });
  action("backend-info", ({ event }) => {
    event?.stopPropagation();
    select("welcome");
    if (!prefs.inspOpen) setPref("inspOpen", true);
    setPref("inspTab", "attributes");
    invalidate("shell", "inspector");
  });
  action("nav-toggle", ({ arg }) => {
    const nav = prefs.navTab;
    if (arg === "booted") setPref("onlyBooted", !prefs.onlyBooted);
    else if (arg === "recent") setPref("recentOnly", { ...prefs.recentOnly, [nav]: !prefs.recentOnly[nav] });
    else if (arg === "failed") setPref("failedOnly", { ...prefs.failedOnly, [nav]: !prefs.failedOnly[nav] });
    invalidate("nav");
  });
  action("filter-clear", () => {
    state.ui.filter[FILTER_KEY[prefs.navTab]] = "";
    invalidate("nav");
    document.querySelector(".nf-input")?.focus();
  });
  action("find-scope", ({ el }) => {
    openMenu({
      anchor: el, label: "Search scope",
      items: FIND_SCOPES.map((s) => ({ label: s.label, checked: state.ui.findScope === s.id, run: () => { state.ui.findScope = s.id; invalidate("nav"); } }))
    });
  });

  field("filter", ({ value }) => {
    state.ui.filter[FILTER_KEY[prefs.navTab]] = value;
    invalidate("nav");
  });
  field("find", ({ value }) => {
    state.ui.filter.find = value;
    invalidate("nav");
  });

  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;
    const input = event.target.closest?.(".nf-input, .find-input");
    if (!input || !input.value) return;
    event.preventDefault();
    event.stopPropagation();
    const key = input.classList.contains("find-input") ? "find" : FILTER_KEY[prefs.navTab];
    state.ui.filter[key] = "";
    input.value = "";
    invalidate("nav");
  });

  // tab bar keyboard: arrows move, Home/End jump; selection follows focus
  document.addEventListener("keydown", (event) => {
    const tab = event.target.closest?.('#nav-tabs [role="tab"], #bottom-tabs [role="tab"]');
    if (!tab || event.ctrlKey || event.metaKey || event.altKey) return;
    const list = Array.from(tab.parentElement.querySelectorAll('[role="tab"]'));
    const at = list.indexOf(tab);
    let next = -1;
    if (event.key === "ArrowRight") next = (at + 1) % list.length;
    else if (event.key === "ArrowLeft") next = (at - 1 + list.length) % list.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = list.length - 1;
    if (next < 0) return;
    event.preventDefault();
    showNavigator(list[next].dataset.arg);
    requestAnimationFrame(() => tab.parentElement.querySelectorAll('[role="tab"]')[next]?.focus());
  });

  contextMenu("device", ({ el, point }) => openDeviceMenu(el.dataset.id, point));
  contextMenu("job", ({ el, point }) => openJobMenu(el.dataset.id, point));
  contextMenu("run", ({ el, point }) => openRunMenu(el.dataset.id, point));
}

// ---------------------------------------------------------------- context menus

export function deviceMenuItems(id) {
  const d = deviceById(id);
  if (!d) return [];
  const booted = isBooted(d);
  const busy = isTransitioning(d);
  const sim = d.type === "simulator";
  return [
    { label: "Boot", icon: "play", disabled: booted || busy || !sim, run: () => bootDevice(id) },
    { label: "Shut Down", icon: "stop.fill", disabled: !booted || d.status === "busy" || !sim, run: () => shutdownDevice(id) },
    { label: "Screenshot", icon: "camera", disabled: !booted || !sim, run: () => screenshotDevice(id) },
    { label: "Run Tests Here", icon: "play.fill", disabled: !d.canRunTests || !sim, run: () => runOnDevice(id) },
    { separator: true },
    { label: "Copy Simulator UDID", icon: "doc.on.doc", disabled: !d.simulatorUdid, run: () => copy(d.simulatorUdid, "Copied the UDID") },
    { label: "Show in Inspector", icon: "info.circle", run: () => { select("device", id); setPref("inspOpen", true); setPref("inspTab", "attributes"); invalidate("shell", "inspector"); } },
    { separator: true },
    { label: "Delete…", icon: "trash", danger: true, disabled: busy || d.status === "busy", run: () => confirmDeleteDevice(id) }
  ];
}

export function openDeviceMenu(id, point, anchor) {
  const items = deviceMenuItems(id);
  if (items.length) openMenu({ items, point, anchor, label: "Simulator", focusFirst: !!anchor });
}

export function jobMenuItems(id) {
  const job = jobById(id);
  if (!job) return [];
  const active = isActive(job);
  const failed = job.status === "failed" && !!job.summary && job.summary.failed > 0;
  return [
    { label: "Open Report", icon: "doc.text", run: () => openJob(id) },
    { label: "Open Log", icon: "text.alignleft", run: () => openJob(id, { tab: "logs" }) },
    { separator: true },
    { label: "Run Again", icon: "play.fill", disabled: active, run: () => rerunJob(id) },
    { label: "Retry Failed Tests", icon: "arrow.clockwise", disabled: !failed, run: () => retryFailed(id) },
    { label: "Cancel", icon: "stop.fill", disabled: !active, run: () => cancelJob(id) },
    { separator: true },
    { label: "Export JUnit", icon: "square.and.arrow.down", disabled: active || !resultsOf(id) && job.status === "cancelled", run: () => exportJUnit(id) },
    { label: "Copy Job ID", icon: "doc.on.doc", run: () => copy(id, "Copied the job ID") }
  ];
}

export function openJobMenu(id, point, anchor) {
  const items = jobMenuItems(id);
  if (items.length) openMenu({ items, point, anchor, label: jobTitle(jobById(id)), focusFirst: !!anchor });
}

export function openRunMenu(id, point, anchor) {
  const run = runById(id);
  if (!run) return;
  const active = ["queued", "running"].includes(run.status);
  openMenu({
    point, anchor, label: runName(run), focusFirst: !!anchor,
    items: [
      { label: "Open Run Report", icon: "doc.text", run: () => select("run", id) },
      { label: "Run Again", icon: "play.fill", disabled: active, run: () => runScheme({ scheme: run.scheme }) },
      { label: "Cancel Run", icon: "stop.fill", disabled: !active, run: () => cancelRun(id) },
      { separator: true },
      { label: "Copy Run ID", icon: "doc.on.doc", run: () => copy(id, "Copied the run ID") }
    ]
  });
}

