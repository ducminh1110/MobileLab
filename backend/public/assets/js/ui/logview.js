// The Logs editor: xcodebuild output rendered like a source editor. Windowed (only the visible lines are in
// the DOM), so a 50k-line log scrolls as smoothly as a 50-line one. Gutter with line numbers, pass/fail diamonds on
// Test Case lines and breakpoint-style markers on lines the user clicked, log syntax colours, the current line
// highlighted, red inline pill on failure lines, live follow with a "Jump to end" pill.

import { esc, html } from "../core/util.js";
import { icon } from "../core/icons.js";

const ROW = 17;
const OVERSCAN = 24;
const MAX_WIDTH_CHARS = 1400;

// ---------------------------------------------------------------- syntax

const TOKEN = new RegExp(
  [
    "(\\*\\* (?:TEST|BUILD) (?:SUCCEEDED|FAILED) \\*\\*)", // 1 result banner
    "(\\b(?:Test Suite|Test Case|Test case|Testing started on|Executed|Command line invocation)\\b)", // 2 keyword
    "('[^']*')", // 3 single quoted
    '("[^"]*")', // 4 double quoted
    "((?:\\/[\\w.@+~\\-]+)+(?:\\.\\w+)(?::\\d+(?::\\d+)?)?)", // 5 path
    "(\\berror:|\\bfailed\\b|\\bFAILED\\b|\\bfatal error\\b)", // 6 error
    "(\\bpassed\\b|\\bSUCCEEDED\\b)", // 7 success
    "(\\bwarning:)", // 8 warning
    "(\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?(?: [+-]\\d{4}|Z)?)", // 9 timestamp
    "(-?\\d+(?:\\.\\d+)?)" // 10 number
  ].join("|"),
  "g"
);

function quoted(text) {
  const inner = text.slice(1, -1);
  const objc = /^-\[(\S+) (\S+)\]$/.exec(inner);
  if (objc) return `<span class="t-str">'-[</span><span class="t-type">${esc(objc[1])}</span> <span class="t-id">${esc(objc[2])}</span><span class="t-str">]'</span>`;
  const swift = /^(?:[\w.]+)\.(\w+)\(\)$/.exec(inner);
  if (swift) return `<span class="t-str">'</span><span class="t-id">${esc(inner)}</span><span class="t-str">'</span>`;
  if (/^\w+Tests?$|^[A-Z]\w*\.\w+$/.test(inner)) return `<span class="t-str">'</span><span class="t-type">${esc(inner)}</span><span class="t-str">'</span>`;
  return `<span class="t-str">${esc(text)}</span>`;
}

export function highlightLine(line) {
  if (!line) return "";
  const text = line.length > 4000 ? line.slice(0, 4000) + "…" : line;
  if (/^\s*(?:\/\/|#)/.test(text)) return `<span class="t-com">${esc(text)}</span>`;
  let out = "";
  let last = 0;
  TOKEN.lastIndex = 0;
  let m;
  while ((m = TOKEN.exec(text))) {
    if (m.index > last) out += esc(text.slice(last, m.index));
    const s = m[0];
    if (m[1]) out += `<span class="${/SUCCEEDED/.test(s) ? "t-ok" : "t-err"} t-bold">${esc(s)}</span>`;
    else if (m[2]) out += `<span class="t-key">${esc(s)}</span>`;
    else if (m[3]) out += quoted(s);
    else if (m[4]) out += `<span class="t-str">${esc(s)}</span>`;
    else if (m[5]) out += `<span class="t-id">${esc(s)}</span>`;
    else if (m[6]) out += `<span class="t-err">${esc(s)}</span>`;
    else if (m[7]) out += `<span class="t-ok">${esc(s)}</span>`;
    else if (m[8]) out += `<span class="t-warn">${esc(s)}</span>`;
    else if (m[9]) out += `<span class="t-com">${esc(s)}</span>`;
    else out += `<span class="t-num">${esc(s)}</span>`;
    last = m.index + s.length;
    if (s.length === 0) TOKEN.lastIndex += 1;
  }
  if (last < text.length) out += esc(text.slice(last));
  return out;
}

const CASE_RESULT = /^\s*Test [Cc]ase '.+' (passed|failed|skipped)/;
const FAILURE_LINE = /^(.+?):(\d+)(?::\d+)?: error: (.*)$/;

/** What a line means for the gutter and the inline pill. */
export function classify(line) {
  const result = CASE_RESULT.exec(line);
  if (result) return { diamond: result[1], failed: result[1] === "failed" };
  const failure = FAILURE_LINE.exec(line);
  if (failure) {
    const rest = failure[3];
    const at = rest.indexOf(" : ");
    return { error: true, pill: (at >= 0 ? rest.slice(at + 3) : rest).slice(0, 220) };
  }
  if (/^\*\* (?:TEST|BUILD) FAILED \*\*/.test(line) || /^(?:xcodebuild: )?error: /.test(line)) return { error: true, pill: line.replace(/^(?:xcodebuild: )?error: /, "").slice(0, 160) };
  return null;
}

export function findRevealLine(lines, { line, caseName, query }) {
  if (line) return Math.min(Math.max(1, line), lines.length) - 1;
  if (caseName) {
    let i = lines.findIndex((l) => l.includes(caseName) && /error:/.test(l));
    if (i < 0) i = lines.findIndex((l) => l.includes(caseName) && /failed/.test(l));
    if (i < 0) i = lines.findIndex((l) => l.includes(caseName));
    return i;
  }
  if (query) {
    let i = lines.findIndex((l) => l.includes(query) && /error/.test(l));
    if (i < 0) i = lines.findIndex((l) => l.includes(query));
    return i;
  }
  return -1;
}

export function firstFailureLine(lines) {
  for (let i = 0; i < lines.length; i += 1) if (FAILURE_LINE.test(lines[i])) return i;
  for (let i = 0; i < lines.length; i += 1) if (/^(?:xcodebuild: )?error: /.test(lines[i])) return i;
  return -1;
}

// ---------------------------------------------------------------- the view

const breakpoints = new Map();

export class LogView {
  constructor(host, model, { jobId }) {
    this.host = host;
    this.model = model;
    this.jobId = jobId;
    this.live = false;
    this.follow = true;
    this.selected = -1;
    this.appliedReveal = null;
    this.raf = 0;
    this.lastKey = "";
    this.pendingReveal = null;
    this.initialised = false;
    this.charW = 7.2;
    this.build();
    this.unsubscribe = model.subscribe((kind, from) => this.onModel(kind, from));
    this.onResize = () => this.schedule(true);
    window.addEventListener("mobilelab:resized", this.onResize);
    this.ro = typeof ResizeObserver === "function" ? new ResizeObserver(() => this.schedule(true)) : null;
    this.ro?.observe(this.scroll);
    this.onModel("reload", 0);
  }

  build() {
    this.host.innerHTML = "";
    const root = document.createElement("div");
    root.className = "logv";
    root.innerHTML = `
      <div class="logv-scroll" tabindex="0" role="log" aria-label="Build and test log" aria-live="off">
        <div class="logv-sizer"><div class="logv-rows"></div></div>
      </div>
      <button type="button" class="jump-pill" hidden>${icon("arrow.down.circle", "ic-14")}<span>Jump to end</span></button>
      <div class="logv-note" role="status"></div>`;
    this.host.appendChild(root);
    this.root = root;
    this.scroll = root.querySelector(".logv-scroll");
    this.sizer = root.querySelector(".logv-sizer");
    this.rows = root.querySelector(".logv-rows");
    this.pill = root.querySelector(".jump-pill");
    this.note = root.querySelector(".logv-note");
    const probe = document.createElement("span");
    probe.className = "logv-probe";
    probe.textContent = "M".repeat(100);
    root.appendChild(probe);
    const w = probe.getBoundingClientRect().width / 100;
    if (w > 4) this.charW = w;
    probe.remove();

    this.scroll.addEventListener("scroll", () => {
      const atEnd = this.scroll.scrollHeight - this.scroll.scrollTop - this.scroll.clientHeight < ROW * 1.5;
      if (!this.programmatic) this.follow = atEnd;
      this.pill.hidden = !(this.live && !this.follow);
      this.schedule();
    }, { passive: true });
    this.pill.addEventListener("click", () => this.toEnd());
    this.rows.addEventListener("click", (event) => {
      const row = event.target.closest(".ll");
      if (!row) return;
      const n = Number(row.dataset.n);
      if (event.target.closest(".lg")) {
        const set = breakpoints.get(this.jobId) || new Set();
        if (set.has(n)) set.delete(n); else set.add(n);
        breakpoints.set(this.jobId, set);
      } else this.selected = n - 1;
      this.schedule(true);
    });
  }

  destroy() {
    this.unsubscribe?.();
    this.ro?.disconnect();
    window.removeEventListener("mobilelab:resized", this.onResize);
    cancelAnimationFrame(this.raf);
    this.host.innerHTML = "";
  }

  setJob(job) {
    const live = !!job && ["queued", "running", "retrying"].includes(job.status);
    if (live !== this.live) {
      this.live = live;
      if (live && !this.initialised) this.follow = true;
      this.pill.hidden = !(this.live && !this.follow);
      this.schedule(true);
    }
  }

  setReveal(reveal) {
    if (!reveal || reveal.jobId !== this.jobId || this.appliedReveal === reveal.nonce) return;
    this.pendingReveal = reveal;
    this.tryReveal();
  }

  tryReveal() {
    if (!this.pendingReveal || !this.model.loaded) return;
    const reveal = this.pendingReveal;
    const index = findRevealLine(this.model.lines, reveal);
    this.appliedReveal = reveal.nonce;
    this.pendingReveal = null;
    if (index >= 0) {
      this.selected = index;
      this.follow = false;
      this.scrollToLine(index, true);
    }
  }

  onModel(kind, from) {
    const lines = this.model.lines;
    const digits = Math.max(3, String(Math.max(1, lines.length)).length);
    this.root.style.setProperty("--gutter-w", `${Math.ceil(digits * this.charW + 30)}px`);
    const chars = Math.min(MAX_WIDTH_CHARS, this.model.maxLen);
    this.sizer.style.height = `${lines.length * ROW + 10}px`;
    this.sizer.style.width = `calc(var(--gutter-w) + ${Math.ceil(chars * this.charW + 120)}px)`;
    this.updateNote();

    if (this.model.loaded && !this.initialised) {
      this.initialised = true;
      if (!this.pendingReveal) {
        const fail = this.live ? -1 : firstFailureLine(lines);
        if (fail >= 0) {
          this.selected = fail;
          this.follow = false;
          this.scrollToLine(fail, true);
        } else this.toEnd();
      }
    }
    if (this.model.loaded) this.tryReveal();
    if (kind === "append" && this.follow && this.initialised) this.toEnd(false);
    if (kind === "reset") { this.selected = -1; this.follow = true; this.toEnd(false); }
    this.schedule(true);
  }

  updateNote() {
    const m = this.model;
    let text = "";
    if (m.error) text = m.error;
    else if (m.loading) text = "Loading the log…";
    else if (!m.lines.length) text = this.live ? "Waiting for output…" : "This job produced no output.";
    else if (m.truncated) text = "Showing the tail of a large log.";
    this.note.textContent = text;
    this.note.hidden = !text;
    this.note.classList.toggle("is-error", !!m.error);
  }

  toEnd(smooth = false) {
    this.follow = true;
    this.pill.hidden = true;
    this.programmatic = true;
    this.scroll.scrollTo({ top: this.scroll.scrollHeight, behavior: smooth ? "smooth" : "auto" });
    requestAnimationFrame(() => { this.programmatic = false; });
    this.schedule(true);
  }

  scrollToLine(index, center) {
    const target = index * ROW - (center ? Math.max(0, (this.scroll.clientHeight - ROW) / 2) : 0);
    this.programmatic = true;
    this.scroll.scrollTop = Math.max(0, target);
    requestAnimationFrame(() => { this.programmatic = false; });
    this.schedule(true);
  }

  schedule(force = false) {
    if (force) this.lastKey = "";
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => { this.raf = 0; this.paint(); });
  }

  paint() {
    const lines = this.model.lines;
    const top = this.scroll.scrollTop;
    const height = this.scroll.clientHeight || 400;
    const first = Math.max(0, Math.floor(top / ROW) - OVERSCAN);
    const last = Math.min(lines.length, Math.ceil((top + height) / ROW) + OVERSCAN);
    const key = `${first}:${last}:${this.model.version}:${this.selected}:${this.live}:${(breakpoints.get(this.jobId) || new Set()).size}`;
    if (key === this.lastKey) return;
    this.lastKey = key;

    const marks = breakpoints.get(this.jobId);
    const current = this.selected >= 0 ? this.selected : lines.length - 1;
    let out = "";
    for (let i = first; i < last; i += 1) {
      const line = lines[i];
      const meta = classify(line);
      const n = i + 1;
      const isCur = i === current;
      const cls = ["ll"];
      if (meta?.error) cls.push("err");
      if (isCur && !meta?.error) cls.push("cur");
      if (i === this.selected) cls.push("sel");
      if (marks?.has(n)) cls.push("bp");
      let mark = "";
      if (meta?.diamond === "passed") mark = icon("checkmark.diamond.fill", "ic-10 c-pass");
      else if (meta?.diamond === "failed") mark = icon("xmark.diamond.fill", "ic-10 c-fail");
      else if (meta?.diamond === "skipped") mark = icon("minus.diamond", "ic-10 c-warn");
      let pill = "";
      if (meta?.pill) pill = `<span class="pill pill-err">${icon("xmark.circle.fill", "ic-12")}<span>${esc(meta.pill)}</span></span>`;
      else if (isCur && this.live) pill = `<span class="pill pill-run"><span class="spin spin-sm"></span><span>Running</span></span>`;
      out += `<div class="${cls.join(" ")}" data-n="${n}"><span class="lg"><span class="lg-mark">${mark}</span><span class="lg-n">${n}</span></span><span class="lc">${highlightLine(line)}</span>${pill}</div>`;
    }
    this.rows.style.transform = `translateY(${first * ROW}px)`;
    this.rows.innerHTML = out;
    this.pill.hidden = !(this.live && !this.follow);
  }

  text() { return this.model.lines.join("\n"); }
}

export const logHostHtml = () => html`<div class="log-host" id="log-host" data-morph="static"></div>`;
