// Jump bar: grid button, back / forward through what was opened, and a breadcrumb path whose crumbs are
// popups listing siblings (Xcode's jump bar). Far right: related items (Settings), editor options (Inspector), add.

import { html } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, setPref, select, invalidate, onRender, deviceById, jobById, runById, historyGo, canGoBack, canGoForward } from "../core/state.js";
import { deviceGroups, deviceIcon, deviceRuntime, jobDestinationName, jobStatus, runName, schemeNames } from "../core/models.js";
import { hooks, showNavigator, togglePanel, openJob } from "../core/actions.js";
import { setExpanded } from "../core/state.js";
import { action } from "./dispatch.js";
import { openMenu } from "./menu.js";

const TAB_LABEL = { summary: "Summary", tests: "Tests", logs: "Logs" };

function rootItems() {
  return [
    { label: "Welcome", icon: "mark", checked: state.sel.kind === "welcome", run: () => select("welcome") },
    { separator: true },
    { label: "Simulators", icon: "iphone", run: () => showNavigator("devices") },
    { label: "Runs", icon: "diamond", run: () => showNavigator("tests") },
    { label: "Issues", icon: "exclamationmark.triangle", run: () => showNavigator("issues") },
    { label: "Reports", icon: "doc.text", run: () => showNavigator("reports") }
  ];
}

function crumbsFor(sel) {
  const root = { label: "MobileLab", icon: icon("mark", "ic-14"), items: rootItems };
  if (sel.kind === "welcome") return [root, { label: "Welcome", icon: icon("mark", "ic-14"), items: () => rootItems() }];
  if (sel.kind === "device") {
    const device = deviceById(sel.id);
    if (!device) return [root];
    const group = deviceGroups().find((g) => g.key === device.runtime);
    const crumbs = [root, {
      label: "Simulators", icon: icon("folder.fill", "ic-14"),
      items: () => deviceGroups().map((g) => ({ label: g.name, icon: "folder.fill", run: () => { setExpanded("devices", `rt:${g.key}`, true); showNavigator("devices"); } }))
    }];
    if (device.type === "vm") return [...crumbs.slice(0, 1), { label: "Experimental VMs", icon: icon("folder.fill", "ic-14"), items: () => [] }, { label: device.name, icon: icon("cpu", "ic-14"), items: () => [] }];
    crumbs.push({
      label: deviceRuntime(device), icon: icon("folder.fill", "ic-14"),
      items: () => (group?.devices || []).map((d) => ({ label: d.name, icon: deviceIcon(d), checked: d.id === device.id, run: () => select("device", d.id) }))
    });
    crumbs.push({
      label: device.name, icon: icon(deviceIcon(device), "ic-14"),
      items: () => (group?.devices || []).map((d) => ({ label: d.name, icon: deviceIcon(d), checked: d.id === device.id, run: () => select("device", d.id) }))
    });
    return crumbs;
  }
  if (sel.kind === "job") {
    const job = jobById(sel.id);
    if (!job) return [root];
    return [
      root,
      { label: "Reports", icon: icon("folder.fill", "ic-14"), items: () => [{ label: "Show Report Navigator", run: () => showNavigator("reports") }] },
      {
        label: job.testTarget, icon: icon("folder.fill", "ic-14"),
        items: () => schemeNames().map((name) => ({
          label: name, checked: name === job.testTarget,
          run: () => { const latest = state.jobs.find((j) => j.testTarget === name); if (latest) openJob(latest.id); }
        }))
      },
      { label: jobDestinationName(job), icon: statusIcon(job), items: () => state.jobs.filter((j) => j.testTarget === job.testTarget).slice(0, 12).map((j) => ({ label: `${jobDestinationName(j)} · ${jobStatus(j).label}`, checked: j.id === job.id, run: () => openJob(j.id) })) },
      { label: TAB_LABEL[prefs.reportTab] || "Summary", icon: "", items: () => Object.entries(TAB_LABEL).map(([id, label]) => ({ label, checked: prefs.reportTab === id, run: () => { setPref("reportTab", id); invalidate("editor", "jump"); } })) }
    ];
  }
  if (sel.kind === "run") {
    const run = runById(sel.id);
    if (!run) return [root];
    return [
      root,
      { label: "Runs", icon: icon("folder.fill", "ic-14"), items: () => [{ label: "Show Test Navigator", run: () => showNavigator("tests") }] },
      { label: runName(run), icon: icon("diamond", "ic-14"), items: () => state.runs.slice(0, 12).map((r) => ({ label: runName(r), checked: r.id === run.id, run: () => select("run", r.id) })) }
    ];
  }
  return [root];
}

function statusIcon(job) {
  const def = jobStatus(job);
  return def.spinner ? html`<span class="spin spin-sm ${def.cls}"></span>` : icon(def.icon, `ic-14 ${def.cls}`);
}

let currentCrumbs = [];

function jumpHtml() {
  currentCrumbs = crumbsFor(state.sel);
  return html`
    <button type="button" class="jb-btn" data-action="open-quickly" title="Open Quickly" aria-label="Open Quickly">${icon("square.grid.2x2", "ic-14")}</button>
    <span class="jb-sep"></span>
    <button type="button" class="jb-btn" data-action="nav-back" ${canGoBack() ? "" : html`disabled`} title="Go Back" aria-label="Go back">${icon("chevron.left", "ic-14")}</button>
    <button type="button" class="jb-btn" data-action="nav-forward" ${canGoForward() ? "" : html`disabled`} title="Go Forward" aria-label="Go forward">${icon("chevron.right", "ic-14")}</button>
    <nav class="crumbs" aria-label="Path">
      ${currentCrumbs.map((c, i) => html`${i ? html`<span class="crumb-sep">${icon("chevron.right", "ic-10")}</span>` : ""}<button type="button" class="crumb${i === currentCrumbs.length - 1 ? " last" : ""}" data-action="crumb" data-arg="${i}" aria-haspopup="menu" aria-expanded="false" data-key="crumb-${i}">${c.icon || ""}<span>${c.label}</span></button>`)}
    </nav>
    <span class="jb-right">
      <button type="button" class="jb-btn" data-action="open-settings" title="Related Items: Settings" aria-label="Settings">${icon("arrow.left.arrow.right", "ic-14")}</button>
      <button type="button" class="jb-btn" data-action="jb-inspector" title="Editor Options: Inspector" aria-label="Toggle inspector">${icon("list.bullet.indent", "ic-14")}</button>
      <span class="jb-sep"></span>
      <button type="button" class="jb-btn" data-action="jb-add" title="Add…" aria-label="Add" aria-haspopup="menu">${icon("plus", "ic-14")}</button>
    </span>`;
}

onRender("jump", () => patch(document.getElementById("jumpbar"), jumpHtml()));

export function initJumpbar() {
  action("nav-back", () => historyGo(-1));
  action("nav-forward", () => historyGo(1));
  action("open-quickly", () => hooks.sheet("quick"));
  action("open-settings", () => hooks.sheet("settings", {}));
  action("jb-inspector", () => togglePanel("inspector"));
  action("jb-add", ({ el }) => openMenu({
    anchor: el, align: "end", label: "Add",
    items: [
      { label: "New Simulator…", icon: "iphone", run: () => hooks.sheet("simulator") },
      { label: "New Scheme…", run: () => hooks.sheet("scheme", { isNew: true }) },
      { label: "Run Tests…", icon: "play.fill", run: () => hooks.sheet("run") }
    ]
  }));
  action("crumb", ({ el }) => {
    const crumb = currentCrumbs[Number(el.dataset.arg)];
    if (!crumb) return;
    const items = crumb.items();
    if (!items.length) return;
    openMenu({ anchor: el, items, label: crumb.label });
  });
}
