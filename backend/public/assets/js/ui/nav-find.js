// Find navigator: a search field with a scope popup (Devices, Tests, Events, Log of the open job) and results
// grouped the way Xcode's Find navigator groups them, with the match highlighted.

import { html, highlightSubstring, fmtTimeSec } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state, jobById, invalidate } from "../core/state.js";
import { CASE_STATUS, deviceGroups, deviceIcon, deviceModel, deviceRuntime, jobStatus, jobTitle, suitesOf } from "../core/models.js";
import { ensureResults, resultsOf } from "../core/sync.js";
import { acquireLog, releaseLog, peekLog } from "../core/logstore.js";
import { treeHtml } from "./tree.js";
import { emptyState, statusGlyph, deviceDot } from "./nav-common.js";

export const FIND_SCOPES = [
  { id: "devices", label: "Devices" },
  { id: "tests", label: "Tests" },
  { id: "events", label: "Events" },
  { id: "log", label: "Log of the open job" }
];

const MAX_ROWS = 400;
let held = null;
let unsubscribe = null;

/** The Log scope needs the open job's log in memory; hold it only while that scope is showing. */
export function syncFindLog(active) {
  const want = active && state.sel.kind === "job" ? state.sel.id : null;
  if (held === want) return;
  if (unsubscribe) { unsubscribe(); unsubscribe = null; }
  if (held) releaseLog(held);
  held = want;
  if (held) {
    const model = acquireLog(held);
    unsubscribe = model.subscribe(() => invalidate("nav"));
  }
}

const has = (text, q) => String(text ?? "").toLowerCase().includes(q);

function head() {
  const scope = FIND_SCOPES.find((s) => s.id === state.ui.findScope) || FIND_SCOPES[0];
  return html`<div class="find-head" data-key="find-head">
    <button type="button" class="popup" data-action="find-scope" aria-haspopup="menu" aria-label="Search scope: ${scope.label}"><span>${scope.label}</span>${icon("chevron.up.chevron.down", "ic-10")}</button>
    <div class="find-field"><span class="find-icon">${icon("magnifyingglass", "ic-14")}</span>
      <input type="search" class="find-input" data-key="find-input" data-scope="find" data-field="query" value="${state.ui.filter.find}" placeholder="Search ${scope.label.toLowerCase()}" aria-label="Search" autocomplete="off" spellcheck="false">
    </div>
  </div>`;
}

function group(key, label, count, rows, kids) {
  rows.push({ nav: "find", key, level: 0, expandable: true, expanded: true, label, secondary: `${count}`, cls: "group" });
  rows.push(...kids);
}

function searchDevices(q, rows) {
  let total = 0;
  for (const g of deviceGroups()) {
    const kids = g.devices.filter((d) => has(`${d.name} ${deviceRuntime(d)} ${deviceModel(d)} ${d.simulatorUdid || ""} ${d.id}`, q));
    if (!kids.length) continue;
    total += kids.length;
    group(`rt:${g.key}`, g.name, kids.length, rows, kids.map((d) => ({
      nav: "find", key: `dev:${d.id}`, level: 1, open: { kind: "device", id: d.id }, sel: { kind: "device", id: d.id }, ctx: "device",
      icon: icon(deviceIcon(d), "ic-16"), label: highlightSubstring(d.name, q), secondary: deviceModel(d), trailing: deviceDot(d)
    })));
  }
  return total;
}

function searchTests(q, rows) {
  let total = 0;
  const recent = state.jobs.slice(0, 40);
  for (const job of recent) ensureResults(job.id);
  const jobHits = state.jobs.filter((j) => has(`${jobTitle(j)} ${j.id} ${jobStatus(j).label}`, q));
  if (jobHits.length) {
    total += jobHits.length;
    group("jobs", "Jobs", jobHits.length, rows, jobHits.slice(0, 60).map((j) => ({
      nav: "find", key: `job:${j.id}`, level: 1, open: { kind: "job", id: j.id }, sel: { kind: "job", id: j.id }, ctx: "job",
      icon: statusGlyph(jobStatus(j)), label: highlightSubstring(jobTitle(j), q)
    })));
  }
  for (const job of recent) {
    const cases = (resultsOf(job.id)?.cases || []).filter((c) => has(`${c.name} ${c.className} ${c.message || ""}`, q));
    if (!cases.length) continue;
    total += cases.length;
    group(`cases:${job.id}`, jobTitle(job), cases.length, rows, cases.map((c) => ({
      nav: "find", key: `case:${job.id}:${c.className}/${c.name}`, level: 1, open: { kind: "job", id: job.id, tab: "logs", caseName: c.name },
      icon: statusGlyph(CASE_STATUS[c.status]), label: highlightSubstring(c.name, q), secondary: suitesOf([c])[0].name
    })));
    if (rows.length > MAX_ROWS) break;
  }
  return total;
}

function searchEvents(q, rows) {
  const hits = [];
  for (let i = state.events.length - 1; i >= 0 && hits.length < 200; i -= 1) {
    const e = state.events[i];
    if (has(`${e.message} ${e.action} ${e.source}`, q)) hits.push(e);
  }
  if (!hits.length) return 0;
  const bySource = new Map();
  for (const e of hits) { if (!bySource.has(e.source)) bySource.set(e.source, []); bySource.get(e.source).push(e); }
  for (const [source, list] of bySource) {
    group(`src:${source}`, source, list.length, rows, list.map((e) => ({
      nav: "find", key: `ev:${e.id}`, level: 1,
      open: e.jobId ? { kind: "job", id: e.jobId, tab: "logs" } : e.deviceId ? { kind: "device", id: e.deviceId } : undefined,
      icon: icon(e.type === "error" ? "exclamationmark.triangle.fill" : "info.circle", `ic-16 ${e.type === "error" ? "c-fail" : "c-secondary"}`),
      label: highlightSubstring(e.message, q), secondary: fmtTimeSec(e.timestamp), title: e.message
    })));
  }
  return hits.length;
}

function searchLog(q, rows) {
  if (state.sel.kind !== "job") return -1;
  const model = peekLog(state.sel.id);
  const job = jobById(state.sel.id);
  if (!model || !job) return 0;
  const kids = [];
  let total = 0;
  for (let i = 0; i < model.lines.length; i += 1) {
    if (!model.lines[i].toLowerCase().includes(q)) continue;
    total += 1;
    if (kids.length < MAX_ROWS) {
      kids.push({
        nav: "find", key: `line:${i}`, level: 1, open: { kind: "job", id: job.id, tab: "logs", line: i + 1 },
        icon: html`<span class="line-no">${i + 1}</span>`, label: highlightSubstring(model.lines[i].trim().slice(0, 300), q), title: model.lines[i]
      });
    }
  }
  if (total) group("log", `Log of ${jobTitle(job)}`, total, rows, kids);
  return total;
}

export function findView() {
  const q = state.ui.filter.find.trim().toLowerCase();
  const scope = state.ui.findScope;
  syncFindLog(scope === "log");
  const top = head();
  if (!q) {
    return { html: html`${top}${emptyState({ iconName: "magnifyingglass", title: "Find", text: scope === "log" ? "Type to search the log of the open report." : `Type to search ${FIND_SCOPES.find((s) => s.id === scope).label.toLowerCase()}.` })}`, count: 0 };
  }
  const rows = [];
  let total = 0;
  if (scope === "devices") total = searchDevices(q, rows);
  else if (scope === "tests") total = searchTests(q, rows);
  else if (scope === "events") total = searchEvents(q, rows);
  else {
    total = searchLog(q, rows);
    if (total === -1) return { html: html`${top}${emptyState({ iconName: "doc.text", title: "No report open", text: "Open a report, then search its log here." })}`, count: 0 };
    const model = peekLog(state.sel.id);
    if (model && model.loading) return { html: html`${top}<div class="stale-note"><span class="spin spin-sm"></span> Loading the log…</div>`, count: 0 };
  }
  if (!total) return { html: html`${top}${emptyState({ iconName: "magnifyingglass", title: "No results", text: `Nothing matches “${state.ui.filter.find.trim()}”.` })}`, count: 0 };
  const capped = rows.length > MAX_ROWS * 2 ? rows.slice(0, MAX_ROWS * 2) : rows;
  return { html: html`${top}${treeHtml(capped, { nav: "find", label: "Find results" })}<div class="stale-note">${total} result${total === 1 ? "" : "s"}</div>`, count: total };
}
