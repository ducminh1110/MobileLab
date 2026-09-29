// Timers that tick in place. Markup carries the start time; a single interval rewrites the text of
// [data-elapsed] (mm:ss since a job started) every second and [data-rel] (relative times) every 15 seconds,
// without re-rendering anything (the render functions compute the same text, so the two never disagree).

import { html } from "../core/util.js";
import { fmtClock, relTime } from "../core/util.js";

export function elapsedSpan(iso) {
  return html`<span class="tick" data-elapsed="${iso || ""}">${iso ? fmtClock(Date.now() - Date.parse(iso)) : "00:00"}</span>`;
}

export function relSpan(iso) {
  return html`<time class="tick" data-rel="${iso}" datetime="${iso}">${relTime(iso)}</time>`;
}

let beat = 0;

export function startTicker() {
  setInterval(() => {
    if (document.visibilityState === "hidden") return;
    const now = Date.now();
    beat += 1;
    for (const el of document.querySelectorAll("[data-elapsed]")) {
      const from = el.dataset.elapsed;
      if (!from) continue;
      const text = fmtClock(now - Date.parse(from));
      if (el.textContent !== text) el.textContent = text;
    }
    if (beat % 15 === 0) {
      for (const el of document.querySelectorAll("[data-rel]")) {
        const text = relTime(el.dataset.rel, now);
        if (el.textContent !== text) el.textContent = text;
      }
    }
  }, 1000);
}
