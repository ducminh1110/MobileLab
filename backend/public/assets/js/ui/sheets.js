// Modal sheets (slide down from the top like Xcode's): New Simulator, Scheme editor, Settings (General,
// Environment, Storage), API token, Keyboard Shortcuts, confirmation. One sheet at a time; focus is trapped,
// Escape closes, Enter presses the default button. Form values live in `sheet.form` so re-renders never lose input.

import { html, raw, keyLabel, fmtBytes, fmtTime, plural } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { api, ApiError, token } from "../core/api.js";
import { state, prefs, setPref, invalidate, onRender } from "../core/state.js";
import { commands, hooks, saveScheme, deleteScheme, spawnSimulator, runScheme, runCleanup, chooseScheme } from "../core/actions.js";
import { ensureCatalog, loadDoctor, start } from "../core/sync.js";
import { schemeConfig, schemeNames, currentScheme, compareVersionsDesc, modelLabel } from "../core/models.js";
import { notify } from "../core/notify.js";
import { applyTheme } from "./shell.js";
import { action, field } from "./dispatch.js";
import { openMenu } from "./menu.js";

const registry = new Map();
export function registerSheet(name, def) { registry.set(name, def); }

const sheetLayer = () => document.getElementById("sheet-layer");

export function currentSheet() { return state.ui.sheet; }

export function openSheet(name, props = {}) {
  if (name === "run") { void runScheme(); return; }
  const def = registry.get(name);
  if (!def) return;
  if (state.ui.sheet && state.ui.sheet.name === name && name !== "confirm") return;
  const sheet = { name, props, def, form: def.init ? def.init(props) : {}, error: "", busy: false, opener: document.activeElement };
  state.ui.sheet = sheet;
  invalidate("overlay");
  if (def.onOpen) def.onOpen(sheet);
  requestAnimationFrame(() => requestAnimationFrame(() => focusInitial()));
}

export function closeSheet() {
  const sheet = state.ui.sheet;
  if (!sheet) return;
  state.ui.sheet = null;
  sheet.def.onClose?.(sheet);
  invalidate("overlay");
  const back = sheet.opener;
  requestAnimationFrame(() => { if (back && document.contains(back)) back.focus({ preventScroll: true }); });
}

export function setSheetError(message) {
  if (!state.ui.sheet) return;
  state.ui.sheet.error = message || "";
  invalidate("overlay");
}

function focusInitial() {
  const layer = sheetLayer();
  const target = layer.querySelector("[data-autofocus]") || layer.querySelector("input:not([type=hidden]), textarea, select, button.btn-primary") || layer.querySelector(".sheet");
  target?.focus({ preventScroll: true });
}

const focusables = (root) => Array.from(root.querySelectorAll('button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])')).filter((el) => el.offsetParent !== null || el === document.activeElement);

function sheetHtml(sheet) {
  const { def } = sheet;
  const size = def.size || "md";
  if (def.palette) {
    return html`<div class="scrim scrim-light" data-action="sheet-scrim" data-key="scrim"></div>
      <div class="sheet sheet-${sheet.name} palette" role="dialog" aria-modal="true" aria-label="${def.title}" tabindex="-1" data-key="sheet-${sheet.name}">${def.render(sheet)}</div>`;
  }
  return html`
    <div class="scrim" data-action="sheet-scrim" data-key="scrim"></div>
    <div class="sheet sheet-${sheet.name} sheet-${size}" role="dialog" aria-modal="true" aria-labelledby="sheet-title" tabindex="-1" data-key="sheet-${sheet.name}">
      <header class="sheet-head">
        <h2 id="sheet-title">${typeof def.title === "function" ? def.title(sheet) : def.title}</h2>
        <button type="button" class="icon-btn icon-btn-sm sheet-x" data-action="sheet-close" aria-label="Close" title="Close (Esc)">${icon("xmark", "ic-14")}</button>
      </header>
      <div class="sheet-body">${def.render(sheet)}</div>
      ${sheet.error ? html`<div class="sheet-error" role="alert">${icon("exclamationmark.triangle.fill", "ic-14")}<span>${sheet.error}</span></div>` : ""}
      ${def.footer ? html`<footer class="sheet-foot">${def.footer(sheet)}</footer>` : ""}
    </div>`;
}

onRender("overlay", () => {
  const sheet = state.ui.sheet;
  const layer = sheetLayer();
  document.getElementById("app").toggleAttribute("data-modal", !!sheet);
  patch(layer, sheet ? sheetHtml(sheet) : "");
});

// ---------------------------------------------------------------- shared bits

function popupField(label, scope, value, hint) {
  return html`<div class="frow"><label class="flabel" for="pf-${scope}">${label}</label>
    <div class="fctl"><button type="button" class="popup popup-wide" id="pf-${scope}" data-action="sheet-popup" data-arg="${scope}" aria-haspopup="menu"><span>${value}</span>${icon("chevron.up.chevron.down", "ic-10")}</button>${hint ? html`<p class="fhint">${hint}</p>` : ""}</div></div>`;
}

function textField(label, name, value, { placeholder = "", hint = "", mono = false, autofocus = false, type = "text", error = "" } = {}) {
  return html`<div class="frow"><label class="flabel" for="sf-${name}">${label}</label>
    <div class="fctl"><input class="field${mono ? " field-mono" : ""}${error ? " field-error" : ""}" id="sf-${name}" type="${type}" data-scope="sheet" data-field="${name}" data-key="sf-${name}" value="${value ?? ""}" placeholder="${placeholder}" autocomplete="off" spellcheck="false" ${autofocus ? raw("data-autofocus") : ""} ${error ? raw('aria-invalid="true"') : ""}>
    ${error ? html`<p class="fhint err-inline">${error}</p>` : hint ? html`<p class="fhint">${hint}</p>` : ""}</div></div>`;
}

// ---------------------------------------------------------------- New Simulator

registerSheet("simulator", {
  title: "New Simulator",
  size: "md",
  init: () => ({ runtime: "", model: "", name: "" }),
  onOpen: (sheet) => {
    void ensureCatalog({ force: true }).then((catalog) => {
      if (!catalog || state.ui.sheet !== sheet) return;
      const runtimes = [...(catalog.runtimes || [])].sort((a, b) => compareVersionsDesc(a.name, b.name));
      if (!sheet.form.runtime && runtimes[0]) sheet.form.runtime = runtimes[0].identifier;
      const types = catalog.deviceTypes || [];
      if (!sheet.form.model) sheet.form.model = (types.find((t) => t.family === "iPhone") || types[0])?.identifier || "";
      invalidate("overlay");
    });
  },
  render: (sheet) => {
    const cat = state.catalog;
    const f = sheet.form;
    if (!cat) return html`<p class="fhint">${state.catalogError ? html`<span class="err-inline" role="alert">${state.catalogError}</span>` : html`<span class="spin spin-sm"></span> Reading the runtimes and device types this host offers…`}</p>`;
    const rt = cat.runtimes.find((r) => r.identifier === f.runtime);
    const ty = cat.deviceTypes.find((t) => t.identifier === f.model);
    const cap = state.capacity;
    const placeholder = ty && rt ? `${ty.name} (${rt.name})` : "Simulator name";
    return html`
      ${popupField("Runtime", "runtime", rt ? rt.name : "No runtime available")}
      ${popupField("Device Type", "model", ty ? ty.name : "No device type available")}
      ${textField("Name", "name", f.name, { placeholder, hint: "Leave empty to use the suggested name.", autofocus: true })}
      <p class="fhint fsource">${cat.source === "demo" ? "Demo catalog: these runtimes and device types are simulated." : "Read from simctl on this host."}${cap ? ` A simulator uses 1 capacity unit (${cap.load} of ${cap.maxLoad} in use).` : ""}</p>`;
  },
  footer: (sheet) => html`<button type="button" class="btn btn-lg" data-action="sheet-close">Cancel</button>
    <button type="button" class="btn btn-lg btn-primary" data-action="sim-create" ${state.catalog && sheet.form.runtime && sheet.form.model && !sheet.busy ? "" : raw("disabled")} data-default>${sheet.busy ? "Creating…" : "Create"}</button>`
});

// ---------------------------------------------------------------- Scheme editor

function newSchemeName() {
  const base = "MyScheme";
  const names = new Set(schemeNames());
  if (!names.has(base)) return base;
  let n = 2;
  while (names.has(`${base}${n}`)) n += 1;
  return `${base}${n}`;
}

registerSheet("scheme", {
  title: (sheet) => (sheet.props.isNew ? "New Scheme" : `Edit Scheme “${sheet.props.name || currentScheme()}”`),
  size: "lg",
  init: (props) => {
    const name = props.isNew ? "" : props.name || currentScheme();
    const cfg = props.isNew ? {} : schemeConfig(name);
    return {
      name, originalName: props.isNew ? "" : name,
      mode: cfg.workspacePath ? "workspace" : cfg.projectPath ? "project" : "dir",
      path: cfg.workspacePath || cfg.projectPath || "",
      workingDirectory: cfg.workingDirectory || "",
      configuration: cfg.configuration || "",
      onlyTesting: (cfg.onlyTesting || []).join("\n"),
      maxRetries: cfg.maxRetries != null ? String(cfg.maxRetries) : "0",
      maxParallel: cfg.maxParallel != null ? String(cfg.maxParallel) : "",
      autoProvision: cfg.autoProvision !== false,
      exists: !props.isNew
    };
  },
  render: (sheet) => {
    const f = sheet.form;
    const nameError = sheet.touched && !f.name.trim() ? "A scheme needs a name." : f.name.trim().startsWith("-") ? "The name must not start with “-”." : "";
    const demo = (state.health?.mode || state.caps?.mode) === "demo";
    return html`
      ${textField("Name", "name", f.name, { placeholder: newSchemeName(), autofocus: true, error: nameError, hint: demo ? "Demo mode: a name containing “fail”, “flaky”, “missing” or “slow” steers the simulated result. Anything else passes." : "The Xcode scheme to test." })}
      <div class="frow"><span class="flabel">Build from</span>
        <div class="fctl"><div class="segmented" role="radiogroup" aria-label="Build from">
          ${[["dir", "Working Directory"], ["project", "Project"], ["workspace", "Workspace"]].map(([id, label]) => html`<button type="button" role="radio" aria-checked="${f.mode === id ? "true" : "false"}" aria-pressed="${f.mode === id ? "true" : "false"}" data-action="scheme-mode" data-arg="${id}">${label}</button>`)}
        </div></div></div>
      ${f.mode !== "dir" ? textField(f.mode === "project" ? ".xcodeproj path" : ".xcworkspace path", "path", f.path, { placeholder: f.mode === "project" ? "/path/to/App.xcodeproj" : "/path/to/App.xcworkspace", mono: true }) : ""}
      ${textField("Working Directory", "workingDirectory", f.workingDirectory, { placeholder: "Where xcodebuild runs (default: the backend’s workspace)", mono: true, hint: "Used for Swift packages and single-project folders." })}
      ${textField("Configuration", "configuration", f.configuration, { placeholder: "Debug" })}
      <div class="frow"><label class="flabel" for="sf-onlyTesting">Only Testing</label>
        <div class="fctl"><textarea class="field field-mono" id="sf-onlyTesting" rows="3" data-scope="sheet" data-field="onlyTesting" data-key="sf-onlyTesting" placeholder="One filter per line, for example AppTests/LoginTests" spellcheck="false">${f.onlyTesting}</textarea><p class="fhint">Empty runs every test.</p></div></div>
      <div class="frow frow-inline"><label class="flabel" for="sf-maxRetries">Retries</label>
        <div class="fctl fctl-row"><input class="field field-num" id="sf-maxRetries" type="number" min="0" max="5" data-scope="sheet" data-field="maxRetries" data-key="sf-maxRetries" value="${f.maxRetries}">
          <label class="flabel flabel-inline" for="sf-maxParallel">Max parallel</label><input class="field field-num" id="sf-maxParallel" type="number" min="1" max="32" placeholder="Auto" data-scope="sheet" data-field="maxParallel" data-key="sf-maxParallel" value="${f.maxParallel}"></div></div>
      <div class="frow"><span class="flabel"></span><div class="fctl"><label class="check"><input type="checkbox" data-scope="sheet" data-field="autoProvision" data-key="sf-auto" ${f.autoProvision ? raw("checked") : ""}> Create a simulator on demand when none matches</label></div></div>`;
  },
  footer: (sheet) => html`
    ${sheet.form.exists && prefs.schemes.some((s) => s.name === sheet.form.originalName) ? html`<button type="button" class="btn btn-lg btn-danger foot-left" data-action="scheme-delete">Delete Scheme</button>` : ""}
    <button type="button" class="btn btn-lg" data-action="sheet-close">Cancel</button>
    ${sheet.props.runAfterSave ? html`<button type="button" class="btn btn-lg btn-primary" data-action="scheme-save" data-arg="run" data-default>Save and Run</button>` : html`<button type="button" class="btn btn-lg btn-primary" data-action="scheme-save" data-default>Save</button>`}`
});

function schemeFromForm(f) {
  const cfg = { name: f.name.trim(), originalName: f.originalName || undefined, autoProvision: !!f.autoProvision };
  if (f.mode === "project" && f.path.trim()) cfg.projectPath = f.path.trim();
  if (f.mode === "workspace" && f.path.trim()) cfg.workspacePath = f.path.trim();
  if (f.workingDirectory.trim()) cfg.workingDirectory = f.workingDirectory.trim();
  if (f.configuration.trim()) cfg.configuration = f.configuration.trim();
  const only = f.onlyTesting.split("\n").map((l) => l.trim()).filter(Boolean);
  if (only.length) cfg.onlyTesting = only;
  const retries = Number.parseInt(f.maxRetries, 10);
  cfg.maxRetries = Number.isInteger(retries) ? Math.min(5, Math.max(0, retries)) : 0;
  const par = Number.parseInt(f.maxParallel, 10);
  if (Number.isInteger(par) && par > 0) cfg.maxParallel = Math.min(32, par);
  return cfg;
}

// ---------------------------------------------------------------- Settings

const PANES = [
  { id: "general", label: "General", icon: "gearshape" },
  { id: "environment", label: "Environment", icon: "stethoscope" },
  { id: "storage", label: "Storage", icon: "internaldrive" }
];

const CHECK_ICON = { ok: ["checkmark.circle.fill", "c-pass"], warn: ["exclamationmark.triangle.fill", "c-warn"], fail: ["xmark.circle.fill", "c-fail"], skip: ["minus", "c-dim"] };

function generalPane(sheet) {
  const c = state.caps;
  return html`
    <div class="frow"><span class="flabel">Appearance</span><div class="fctl"><div class="segmented" role="radiogroup" aria-label="Appearance">
      ${[["system", "System"], ["light", "Light"], ["dark", "Dark"]].map(([id, label]) => html`<button type="button" role="radio" aria-checked="${prefs.theme === id ? "true" : "false"}" aria-pressed="${prefs.theme === id ? "true" : "false"}" data-action="theme" data-arg="${id}">${label}</button>`)}
    </div><p class="fhint">System follows your operating system’s appearance.</p></div></div>
    <div class="fsep"></div>
    <div class="frow"><span class="flabel">API URL</span><div class="fctl"><code class="fvalue">${location.origin}</code><p class="fhint">The dashboard talks to the backend that served it.</p></div></div>
    <div class="frow"><label class="flabel" for="sf-token">API Token</label><div class="fctl">
      <div class="fctl-row"><input class="field field-mono" id="sf-token" type="password" data-scope="sheet" data-field="token" data-key="sf-token" value="${sheet.form.token}" placeholder="${token.get() ? "A token is stored in this browser" : c?.auth?.required ? "Required by this backend" : "Not required by this backend"}" autocomplete="off" spellcheck="false">
        <button type="button" class="btn" data-action="token-save" ${sheet.form.token.trim() ? "" : raw("disabled")}>Save</button>
        <button type="button" class="btn" data-action="token-forget" ${token.get() ? "" : raw("disabled")}>Forget</button></div>
      <p class="fhint">Stored in this browser only and sent as a bearer header, never in the address bar.</p></div></div>
    <div class="fsep"></div>
    <div class="frow"><span class="flabel">Layout</span><div class="fctl"><button type="button" class="btn" data-action="reset-layout">Reset Panel Sizes</button><p class="fhint">Sizes of the navigator, inspector and debug area are remembered per browser.</p></div></div>`;
}

function environmentPane() {
  const d = state.doctor;
  return html`
    <div class="pane-head"><div><strong>${d ? { healthy: "Healthy", degraded: "Degraded", unhealthy: "Unhealthy" }[d.status] : "Not checked yet"}</strong><span class="c-secondary">${d ? ` · ${d.mode === "demo" ? "demo mode" : "live"} · checked ${fmtTime(d.generatedAt)}` : ""}</span></div>
      <button type="button" class="btn" data-action="doctor-run" ${state.doctorLoading ? raw("disabled") : ""}>${state.doctorLoading ? html`<span class="spin spin-sm"></span>Checking…` : "Run Again"}</button></div>
    ${state.doctorError ? html`<p class="err-inline" role="alert">${state.doctorError}</p>` : ""}
    ${d ? html`<ul class="checks">${d.checks.map((c) => { const [ic, cls] = CHECK_ICON[c.status] || CHECK_ICON.skip; return html`<li class="check-row" data-key="chk-${c.id}"><span class="${cls}">${icon(ic, "ic-16")}</span><div><strong>${c.name}</strong><p>${c.message}</p>${c.remedy ? html`<p class="remedy"><span>Remedy</span> ${c.remedy}</p>` : ""}</div></li>`; })}</ul>` : state.doctorLoading ? "" : html`<p class="fhint">Press Run Again to check this host.</p>`}`;
}

function storagePane(sheet) {
  const m = state.metrics;
  const r = sheet.form.cleanupResult;
  return html`
    <div class="frow"><span class="flabel">Artifacts</span><div class="fctl"><strong>${m ? fmtBytes(m.artifactBytes) : "…"}</strong><span class="c-secondary"> · ${m ? `${plural(m.jobs, "job")} in history` : "reading…"}</span><p class="fhint">Logs, result bundles and screenshots kept for finished jobs.</p></div></div>
    <div class="fsep"></div>
    <div class="frow"><label class="flabel" for="sf-days">Clean Up</label><div class="fctl">
      <div class="fctl-row"><span>Remove finished jobs older than</span><input class="field field-num" id="sf-days" type="number" min="0" max="3650" data-scope="sheet" data-field="days" data-key="sf-days" value="${sheet.form.days}"><span>days</span>
        <button type="button" class="btn" data-action="cleanup-run" ${sheet.busy ? raw("disabled") : ""}>${sheet.busy ? "Cleaning…" : "Clean Up"}</button></div>
      <p class="fhint">${Number(sheet.form.days) === 0 ? "0 days removes every finished job." : "Running and queued jobs are never removed."}</p>
      ${r ? html`<p class="cleanup-result" role="status">${icon("checkmark.circle.fill", "ic-14 c-pass")} Removed ${plural(r.jobsRemoved, "job")}, ${plural(r.runsRemoved, "run")} and ${plural(r.artifactsRemoved, "artifact")}, freeing ${fmtBytes(r.bytesFreed)}.</p>` : ""}
    </div></div>`;
}

registerSheet("settings", {
  title: "Settings",
  size: "xl",
  init: (props) => ({ pane: props.pane || "general", token: "", days: String(14), cleanupResult: null }),
  onOpen: (sheet) => {
    if (sheet.form.pane === "environment" && !state.doctor) void loadDoctor();
    void refreshMetrics();
  },
  render: (sheet) => html`<div class="settings">
    <nav class="settings-nav" role="tablist" aria-orientation="vertical" aria-label="Settings panes">
      ${PANES.map((p) => html`<button type="button" role="tab" class="sn${sheet.form.pane === p.id ? " selected" : ""}" aria-selected="${sheet.form.pane === p.id ? "true" : "false"}" tabindex="${sheet.form.pane === p.id ? 0 : -1}" data-action="settings-pane" data-arg="${p.id}">${icon(p.icon, "ic-16")}<span>${p.label}</span></button>`)}
    </nav>
    <div class="settings-pane" role="tabpanel">${sheet.form.pane === "environment" ? environmentPane() : sheet.form.pane === "storage" ? storagePane(sheet) : generalPane(sheet)}</div>
  </div>`,
  footer: () => html`<button type="button" class="btn btn-lg btn-primary" data-action="sheet-close" data-default>Done</button>`
});

async function refreshMetrics() {
  try { state.metrics = await api.get("/metrics/summary"); invalidate("overlay"); } catch { /* shown as "reading…" */ }
}

// ---------------------------------------------------------------- API token

registerSheet("auth", {
  title: "API Token Required",
  size: "sm",
  init: (props) => ({ token: "", message: props.message || "" }),
  render: (sheet) => html`
    <p class="sheet-lead">This MobileLab backend is protected. Enter its API token to continue. It is stored in this browser only and sent as a bearer header, never in the address bar.</p>
    ${textField("Token", "token", sheet.form.token, { type: "password", mono: true, autofocus: true, placeholder: "IOSLAB_API_TOKEN" })}
    ${sheet.form.message ? html`<p class="err-inline fmsg" role="alert">${sheet.form.message}</p>` : ""}`,
  footer: (sheet) => html`<button type="button" class="btn btn-lg btn-primary" data-action="auth-submit" ${sheet.form.token.trim() && !sheet.busy ? "" : raw("disabled")} data-default>${sheet.busy ? "Checking…" : "Continue"}</button>`
});

// ---------------------------------------------------------------- confirm

registerSheet("confirm", {
  title: (sheet) => sheet.props.title,
  size: "sm",
  init: () => ({}),
  render: (sheet) => html`<p class="sheet-lead">${sheet.props.message}</p>`,
  footer: (sheet) => html`<button type="button" class="btn btn-lg" data-action="sheet-close" data-autofocus>Cancel</button>
    <button type="button" class="btn btn-lg ${sheet.props.danger ? "btn-danger-solid" : "btn-primary"}" data-action="confirm-ok" data-default>${sheet.props.confirm || "OK"}</button>`
});

// ---------------------------------------------------------------- shortcuts

function shortcutRows() {
  const groups = new Map();
  for (const c of commands) {
    if (!c.keys) continue;
    if (!groups.has(c.group)) groups.set(c.group, []);
    groups.get(c.group).push([c.title, keyLabel(c.keys), true]);
  }
  groups.set("Trees and lists", [
    ["Move between rows", "↑ ↓"], ["Expand / collapse", "→ ←"], ["First / last row", "Home End"], ["Open and focus the editor", "Enter"], ["Open, keep focus", "Space"],
    ["Context menu", "Menu or Shift+F10"], ["Jump to a row by name", "Type a letter"]
  ]);
  groups.set("Panels and menus", [["Resize a divider", "← → ↑ ↓ (Shift for bigger steps)"], ["Reset a divider", "Enter or double-click"], ["Close a menu or sheet", "Esc"]]);
  return groups;
}

registerSheet("shortcuts", {
  title: "Keyboard Shortcuts",
  size: "lg",
  init: () => ({}),
  render: () => html`<div class="shortcuts">${[...shortcutRows()].map(([group, rows]) => html`<section><h3>${group}</h3><dl>${rows.map(([name, keys, isKey]) => html`<div><dt>${name}</dt><dd>${isKey ? html`<kbd class="kbd">${keys}</kbd>` : html`<span class="c-secondary">${keys}</span>`}</dd></div>`)}</dl></section>`)}</div>
    <p class="fhint">On macOS Ctrl reads as ⌘ in the list. Some browsers reserve a few combinations; every action is also in the MobileLab menu.</p>`,
  footer: () => html`<button type="button" class="btn btn-lg btn-primary" data-action="sheet-close" data-default>Done</button>`
});

// ---------------------------------------------------------------- wiring

export function initSheets() {
  hooks.sheet = openSheet;

  action("sheet-close", () => closeSheet());
  action("sheet-scrim", () => { if (state.ui.sheet && !state.ui.sheet.busy && state.ui.sheet.name !== "auth") closeSheet(); });
  action("confirm-ok", () => { const s = state.ui.sheet; closeSheet(); s.props.onConfirm?.(); });

  field("sheet", ({ name, value }) => {
    const sheet = state.ui.sheet;
    if (!sheet) return;
    sheet.form[name] = value;
    if (name === "name") sheet.touched = true;
    if (sheet.name === "quick") sheet.form.active = 0;
    invalidate("overlay");
  });

  action("sheet-popup", ({ el }) => {
    const sheet = state.ui.sheet;
    const cat = state.catalog;
    if (!sheet || !cat) return;
    if (el.dataset.arg === "runtime") {
      const items = [...cat.runtimes].sort((a, b) => compareVersionsDesc(a.name, b.name)).map((r) => ({ label: r.name, checked: sheet.form.runtime === r.identifier, run: () => { sheet.form.runtime = r.identifier; const ok = new Set(r.supportedDeviceTypes || cat.deviceTypes.map((t) => t.identifier)); if (!ok.has(sheet.form.model)) sheet.form.model = (cat.deviceTypes.find((t) => ok.has(t.identifier) && t.family === "iPhone") || cat.deviceTypes.find((t) => ok.has(t.identifier)))?.identifier || ""; invalidate("overlay"); } }));
      openMenu({ anchor: el, items, label: "Runtime", minWidth: el.offsetWidth });
    } else {
      const rt = cat.runtimes.find((r) => r.identifier === sheet.form.runtime);
      const supported = new Set(rt?.supportedDeviceTypes || cat.deviceTypes.map((t) => t.identifier));
      const items = cat.deviceTypes.filter((t) => supported.has(t.identifier)).map((t) => ({ label: t.name || modelLabel(t.identifier), icon: /ipad/i.test(t.name) ? "ipad" : "iphone", checked: sheet.form.model === t.identifier, run: () => { sheet.form.model = t.identifier; invalidate("overlay"); } }));
      openMenu({ anchor: el, items, label: "Device type", minWidth: el.offsetWidth });
    }
  });

  action("sim-create", async () => {
    const sheet = state.ui.sheet;
    if (!sheet || sheet.busy) return;
    sheet.busy = true;
    sheet.error = "";
    invalidate("overlay");
    try {
      await spawnSimulator({ runtime: sheet.form.runtime, modelId: sheet.form.model, name: sheet.form.name.trim() || undefined });
      closeSheet();
    } catch (error) {
      sheet.busy = false;
      sheet.error = error.message;
      invalidate("overlay");
    }
  });

  action("scheme-mode", ({ el }) => { const s = state.ui.sheet; if (s) { s.form.mode = el.dataset.arg; invalidate("overlay"); } });
  action("scheme-save", async ({ el }) => {
    const sheet = state.ui.sheet;
    if (!sheet) return;
    const f = sheet.form;
    sheet.touched = true;
    const name = f.name.trim();
    if (!name) { sheet.error = "A scheme needs a name."; invalidate("overlay"); return; }
    if (name.startsWith("-")) { sheet.error = "The scheme name must not start with “-”."; invalidate("overlay"); return; }
    if (f.mode !== "dir" && !f.path.trim()) { sheet.error = `Enter the path of the ${f.mode === "project" ? ".xcodeproj" : ".xcworkspace"}, or build from the working directory.`; invalidate("overlay"); return; }
    const cfg = schemeFromForm(f);
    saveScheme(cfg);
    chooseScheme(name);
    const runAfter = el.dataset.arg === "run" || sheet.props.runAfterSave;
    closeSheet();
    if (runAfter && el.dataset.arg === "run") void runScheme({ scheme: name });
    else notify(`Saved scheme ${name}.`);
  });
  action("scheme-delete", () => {
    const sheet = state.ui.sheet;
    if (!sheet) return;
    const name = sheet.form.originalName;
    closeSheet();
    deleteScheme(name);
    notify(`Deleted the stored settings of ${name}. It stays in the list while jobs of that name exist.`);
  });

  action("settings-pane", ({ el }) => {
    const sheet = state.ui.sheet;
    if (!sheet) return;
    sheet.form.pane = el.dataset.arg;
    if (el.dataset.arg === "environment" && !state.doctor && !state.doctorLoading) void loadDoctor();
    if (el.dataset.arg === "storage") void refreshMetrics();
    invalidate("overlay");
  });
  action("theme", ({ el }) => { setPref("theme", el.dataset.arg); applyTheme(); invalidate("overlay"); });
  action("reset-layout", () => { setPref("navW", 300); setPref("inspW", 300); setPref("debugH", 240); invalidate("shell"); notify("Panel sizes reset."); });
  action("doctor-run", () => void loadDoctor());
  action("cleanup-run", async () => {
    const sheet = state.ui.sheet;
    if (!sheet || sheet.busy) return;
    const days = Number.parseInt(sheet.form.days, 10);
    if (!Number.isInteger(days) || days < 0 || days > 3650) { sheet.error = "Enter a number of days between 0 and 3650."; invalidate("overlay"); return; }
    sheet.busy = true;
    sheet.error = "";
    invalidate("overlay");
    try {
      sheet.form.cleanupResult = await runCleanup(days);
      await refreshMetrics();
    } catch (error) { sheet.error = error.message; }
    sheet.busy = false;
    invalidate("overlay");
  });

  action("token-save", async () => {
    const sheet = state.ui.sheet;
    if (!sheet) return;
    token.set(sheet.form.token.trim());
    sheet.form.token = "";
    state.auth = "unknown";
    invalidate("overlay");
    await start();
    notify("API token saved in this browser.", { kind: "success" });
  });
  action("token-forget", () => { token.clear(); invalidate("overlay"); notify("The stored API token was removed from this browser."); });

  action("auth-submit", async () => {
    const sheet = state.ui.sheet;
    if (!sheet || sheet.busy) return;
    sheet.busy = true;
    sheet.form.message = "";
    invalidate("overlay");
    token.set(sheet.form.token.trim());
    try {
      await api.get("/capabilities", { quiet401: true });
      state.auth = "ok";
      sheet.busy = false;
      closeSheet();
      await start();
    } catch (error) {
      sheet.busy = false;
      sheet.form.message = error instanceof ApiError && error.status === 401 ? "The backend rejected this token. Check it and try again." : error.message;
      token.clear();
      invalidate("overlay");
    }
  });

  // Escape, Tab trapping, Enter for the default button
  document.addEventListener("keydown", (event) => {
    const sheet = state.ui.sheet;
    if (!sheet || !sheet.def) return;
    if (sheet.def.onKey && sheet.def.onKey(event, sheet)) return;
    if (event.key === "Escape") {
      if (document.querySelector(".menu")) return;
      event.preventDefault();
      if (sheet.name !== "auth" && !sheet.busy) closeSheet();
      return;
    }
    const root = sheetLayer().querySelector(".sheet");
    if (!root) return;
    if (event.key === "Tab") {
      const list = focusables(root);
      if (!list.length) return;
      const first = list[0];
      const last = list[list.length - 1];
      if (!root.contains(document.activeElement)) { event.preventDefault(); first.focus(); }
      else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
      return;
    }
    if (event.key === "Enter" && !event.shiftKey && event.target.tagName !== "BUTTON" && event.target.tagName !== "TEXTAREA") {
      const primary = root.querySelector("[data-default]:not([disabled])");
      if (primary) { event.preventDefault(); primary.click(); }
    }
  });

  // moving between settings panes with the arrow keys
  document.addEventListener("keydown", (event) => {
    const tab = event.target.closest?.(".settings-nav [role=tab]");
    if (!tab) return;
    const list = Array.from(tab.parentElement.querySelectorAll("[role=tab]"));
    const at = list.indexOf(tab);
    let next = -1;
    if (event.key === "ArrowDown") next = (at + 1) % list.length;
    else if (event.key === "ArrowUp") next = (at - 1 + list.length) % list.length;
    if (next < 0) return;
    event.preventDefault();
    list[next].click();
    requestAnimationFrame(() => sheetLayer().querySelectorAll(".settings-nav [role=tab]")[next]?.focus());
  });
}

