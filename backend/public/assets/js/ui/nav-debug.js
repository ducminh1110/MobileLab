// Debug navigator, after Xcode's: a header for the process with gauges (CPU, Memory, Disk, Capacity) that each
// carry a small usage history, then the running jobs as "threads" with their frames. Source: /metrics/summary,
// polled every 2 s while this navigator is visible, and the job list.

import { html, raw, fmtBytes, plural } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state, isExpanded } from "../core/state.js";
import { jobTitle, runName } from "../core/models.js";
import { treeHtml } from "./tree.js";
import { skeleton, emptyState } from "./nav-common.js";
import { elapsedSpan } from "./ticker.js";

const BARS = 40;

function spark(values, max) {
  const bw = 3;
  const gap = 1;
  const h = 10;
  const shown = values.slice(-BARS);
  const bars = shown.map((v, i) => {
    const bh = max > 0 ? Math.max(v > 0 ? 1 : 0, Math.round(h * Math.min(1, v / max))) : 0;
    return `<rect x="${i * (bw + gap)}" y="${h - bh}" width="${bw}" height="${bh}"/>`;
  });
  return raw(`<svg class="spark" viewBox="0 0 ${BARS * (bw + gap)} ${h}" preserveAspectRatio="none" aria-hidden="true">${bars.join("")}</svg>`);
}

function gauge(key, iconName, label, value, values, max, tip) {
  return {
    nav: "debug", key, level: 1, cls: "gauge",
    icon: icon(iconName, "ic-16 c-secondary"), label, trailing: html`<span class="row-time gauge-value">${value}</span>`,
    extra: spark(values, max), title: tip
  };
}

export function debugView() {
  if (state.conn === "offline" && !state.metrics) {
    return { html: emptyState({ iconName: "bolt.horizontal", title: "Backend unreachable", text: "The debug navigator shows the backend process once it is reachable again." }), count: 0 };
  }
  if (!state.metrics) return { html: skeleton(7), count: 0 };
  const m = state.metrics;
  const hist = state.metricsHistory;
  const proc = m.process || {};
  const cap = m.capacity || state.capacity || { load: 0, maxLoad: 0 };
  const online = state.conn === "online";

  const rows = [];
  const rootOpen = isExpanded("debug", "root", true);
  rows.push({
    nav: "debug", key: "root", level: 0, expandable: true, expanded: rootOpen, cls: "root",
    icon: icon("mark", "ic-16"), label: "MobileLab Backend", secondary: `PID ${proc.pid ?? "?"}`,
    trailing: html`<button type="button" class="round-btn" data-action="backend-info" tabindex="-1" title="Show backend details in the inspector" aria-label="Backend details">${icon("info.circle.fill", "ic-14 c-accent")}</button><span class="conn-glyph ${online ? "c-pass" : "c-fail"}" role="img" aria-label="${online ? "Connected" : "Disconnected"}" title="${online ? "Connected" : "Disconnected"}">${icon("bolt.horizontal", "ic-14")}</span>`
  });
  if (rootOpen) {
    rows.push(gauge("cpu", "cpu", "CPU", `${Math.round(proc.cpuPercent ?? 0)}%`, hist.map((s) => s.cpu), 100, "CPU share of the backend process (100% is one full core)"));
    rows.push(gauge("mem", "memorychip", "Memory", fmtBytes(proc.rssBytes ?? 0), hist.map((s) => s.mem), Math.max(1, ...hist.map((s) => s.mem)) * 1.15, "Resident memory of the backend process"));
    rows.push(gauge("disk", "internaldrive", "Disk", fmtBytes(m.artifactBytes ?? 0), hist.map((s) => s.disk), Math.max(1, ...hist.map((s) => s.disk)) * 1.15, "Size of the artifact store (logs, results, screenshots)"));
    rows.push(gauge("cap", "gauge", "Capacity", `${cap.load} of ${cap.maxLoad}`, hist.map((s) => s.load), Math.max(1, cap.maxLoad), `${cap.load} of ${cap.maxLoad} simulator units in use (${cap.cpuCores ?? "?"} cores, ${cap.memoryGb ? Math.round(cap.memoryGb) : "?"} GB RAM)`));

    const running = state.jobs.filter((j) => j.status === "running" || j.status === "retrying").sort((a, b) => (a.startedAt || "") < (b.startedAt || "") ? -1 : 1);
    const queued = state.jobs.filter((j) => j.status === "queued").sort((a, b) => (a.createdAt < b.createdAt ? -1 : 1));
    let n = 0;
    for (const job of running) {
      n += 1;
      const key = `thread:${job.id}`;
      const open = isExpanded("debug", key, true);
      rows.push({
        nav: "debug", key, level: 1, expandable: true, expanded: open, icon: icon("thread", "ic-16 c-accent"),
        label: html`Thread ${n}`, secondary: `Queue: ${job.testTarget}`,
        open: { kind: "job", id: job.id }, sel: { kind: "job", id: job.id }, ctx: "job"
      });
      if (open) {
        const frames = [
          { icon: icon("person.crop.square.fill", "ic-16 c-accent"), label: html`0 ${jobTitle(job)}` },
          { icon: icon("square.stack", "ic-16 c-secondary"), label: `1 attempt ${job.attempts} of ${job.maxRetries + 1}` },
          ...(job.runId ? [{ icon: icon("square.stack", "ic-16 c-secondary"), label: `2 ${runName(state.runs.find((r) => r.id === job.runId) || { scheme: job.testTarget })}` }] : []),
          { icon: icon("clock", "ic-16 c-secondary"), label: html`${job.runId ? 3 : 2} running for ${elapsedSpan(job.startedAt)}` }
        ];
        frames.forEach((f, i) => rows.push({ nav: "debug", key: `${key}:f${i}`, level: 2, icon: f.icon, label: f.label, open: { kind: "job", id: job.id }, sel: i === 0 ? { kind: "job", id: job.id } : undefined }));
      }
    }
    for (const job of queued) {
      n += 1;
      rows.push({
        nav: "debug", key: `queued:${job.id}`, level: 1, dim: true, icon: icon("thread", "ic-16 c-dim"),
        label: html`Thread ${n}`, secondary: `Queue: ${job.testTarget}`, title: `${jobTitle(job)}\n${job.waitingReason || "Waiting for a simulator"}`,
        open: { kind: "job", id: job.id }, ctx: "job"
      });
    }
    if (!running.length && !queued.length) {
      rows.push({ nav: "debug", key: "idle", level: 1, disabled: true, dim: true, label: `No running jobs`, secondary: `${plural(m.jobs ?? 0, "job")} in history` });
    }
  }
  return { html: treeHtml(rows, { nav: "debug", label: "Debug" }), count: rows.length };
}

