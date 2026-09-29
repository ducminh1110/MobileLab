// Open Quickly: a fuzzy palette that jumps to any device, run, job, test case or command.

import { html, fuzzy, highlight, keyLabel, fmtWhen } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state, invalidate, select } from "../core/state.js";
import { commands, isEnabled, openJob } from "../core/actions.js";
import { CASE_STATUS, deviceIcon, deviceModel, deviceRuntime, deviceState, jobStatus, jobTitle, runName, runCountsText, RUN_STATUS, shortClass } from "../core/models.js";
import { registerSheet, closeSheet } from "./sheets.js";
import { action } from "./dispatch.js";
import { statusGlyph } from "./nav-common.js";

const LIMIT = 40;

function collect() {
  const items = [];
  for (const d of state.devices) {
    items.push({ kind: "Device", title: d.name, sub: `${deviceRuntime(d)} · ${deviceModel(d)} · ${deviceState(d).label}`, icon: icon(deviceIcon(d), "ic-16"), run: () => select("device", d.id) });
  }
  for (const r of state.runs) {
    const def = RUN_STATUS[r.status] || RUN_STATUS.queued;
    items.push({ kind: "Run", title: runName(r), sub: `${r.scheme} · ${runCountsText(r)}`, icon: statusGlyph(def), run: () => select("run", r.id) });
  }
  for (const j of state.jobs.slice(0, 200)) {
    items.push({ kind: "Job", title: jobTitle(j), sub: `${jobStatus(j).label} · ${fmtWhen(j.finishedAt || j.updatedAt)}`, icon: statusGlyph(jobStatus(j)), run: () => openJob(j.id) });
  }
  for (const [jobId, entry] of state.results) {
    const job = state.jobs.find((j) => j.id === jobId);
    if (!job || entry.status !== "ok") continue;
    for (const c of entry.data?.cases || []) {
      items.push({ kind: "Test", title: `${shortClass(c.className)}.${c.name}`, sub: jobTitle(job), icon: statusGlyph(CASE_STATUS[c.status]), run: () => openJob(jobId, { tab: "logs", caseName: c.name }) });
    }
  }
  for (const c of commands) {
    items.push({ kind: "Command", title: c.title, sub: c.keys ? keyLabel(c.keys) : c.group, icon: icon("chevron.right", "ic-16"), disabled: !isEnabled(c), run: () => c.run() });
  }
  return items;
}

function results(query) {
  const all = collect();
  if (!query.trim()) {
    const order = { Device: 0, Job: 1, Run: 2, Command: 3, Test: 4 };
    return all.filter((i) => !i.disabled).sort((a, b) => order[a.kind] - order[b.kind]).slice(0, LIMIT).map((i) => ({ ...i, indices: [] }));
  }
  const scored = [];
  for (const item of all) {
    if (item.disabled) continue;
    const m = fuzzy(query.trim(), item.title);
    const s = m ? m.score : item.sub.toLowerCase().includes(query.trim().toLowerCase()) ? 0.5 : null;
    if (s === null) continue;
    scored.push({ ...item, score: s + (item.kind === "Device" ? 2 : item.kind === "Command" ? 1 : 0), indices: m ? m.indices : [] });
  }
  scored.sort((a, b) => b.score - a.score);
  return scored.slice(0, LIMIT);
}

function renderPalette(sheet) {
  const list = results(sheet.form.query);
  sheet.form.list = list;
  sheet.form.active = Math.min(sheet.form.active, Math.max(0, list.length - 1));
  return html`
    <div class="qo-field">${icon("magnifyingglass", "ic-16")}<input class="qo-input" type="text" role="combobox" aria-expanded="true" aria-controls="qo-list" aria-activedescendant="qo-${sheet.form.active}" data-scope="sheet" data-field="query" data-key="qo-input" data-autofocus value="${sheet.form.query}" placeholder="Open Quickly: devices, runs, jobs, tests, commands" aria-label="Open Quickly" autocomplete="off" spellcheck="false"></div>
    <ul class="qo-list" id="qo-list" role="listbox" aria-label="Results">
      ${list.map((item, i) => html`<li class="qo-item${i === sheet.form.active ? " active" : ""}" id="qo-${i}" role="option" aria-selected="${i === sheet.form.active ? "true" : "false"}" data-action="qo-open" data-arg="${i}" data-key="qo-${i}-${item.kind}">
        <span class="qo-ic">${item.icon}</span><span class="qo-txt"><span class="qo-title">${highlight(item.title, item.indices)}</span><span class="qo-sub">${item.sub}</span></span><span class="qo-kind">${item.kind}</span></li>`)}
      ${!list.length ? html`<li class="qo-none" role="presentation">No matches for “${sheet.form.query}”.</li>` : ""}
    </ul>`;
}

function openActive(sheet, index) {
  const item = sheet.form.list?.[index];
  if (!item) return;
  closeSheet();
  requestAnimationFrame(() => item.run());
}

registerSheet("quick", {
  title: "Open Quickly",
  palette: true,
  init: () => ({ query: "", active: 0, list: [] }),
  render: renderPalette,
  onKey: (event, sheet) => {
    const list = sheet.form.list || [];
    if (event.key === "ArrowDown") { event.preventDefault(); sheet.form.active = list.length ? (sheet.form.active + 1) % list.length : 0; invalidate("overlay"); scrollActive(); return true; }
    if (event.key === "ArrowUp") { event.preventDefault(); sheet.form.active = list.length ? (sheet.form.active - 1 + list.length) % list.length : 0; invalidate("overlay"); scrollActive(); return true; }
    if (event.key === "Enter") { event.preventDefault(); openActive(sheet, sheet.form.active); return true; }
    return false;
  }
});

function scrollActive() {
  requestAnimationFrame(() => requestAnimationFrame(() => document.querySelector(".qo-item.active")?.scrollIntoView({ block: "nearest" })));
}

export function initQuick() {
  action("qo-open", ({ el }) => { const sheet = state.ui.sheet; if (sheet?.name === "quick") openActive(sheet, Number(el.dataset.arg)); });
}
