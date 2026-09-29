// Pieces every navigator shares: skeleton rows, empty and error states, status glyphs.

import { html } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state } from "../core/state.js";
import { DEVICE_STATE } from "../core/models.js";

export function skeleton(rows = 8) {
  return html`<div class="skeleton" aria-busy="true" aria-label="Loading">${Array.from({ length: rows }, () => html`<i></i>`)}</div>`;
}

export function emptyState({ iconName = "tray.full", title, text, action, actionLabel, pass = false }) {
  return html`<div class="empty${pass ? " empty-pass" : ""}">
    ${icon(iconName, "ic-32")}
    <strong>${title}</strong>
    ${text ? html`<p>${text}</p>` : ""}
    ${action ? html`<button type="button" class="btn" data-action="${action}">${actionLabel}</button>` : ""}
  </div>`;
}

export function errorState(area, message) {
  return html`<div class="empty">
    ${icon("exclamationmark.triangle", "ic-32")}
    <strong>Could not load ${area}</strong>
    <p class="err-inline" role="alert">${message}</p>
    <button type="button" class="btn" data-action="retry-load">Try Again</button>
  </div>`;
}

/** Status glyph: a diamond for tests (glyph and colour), a spinner while running. */
export function statusGlyph(def, size = "") {
  if (def.spinner) return html`<span class="spin ${def.cls}${size ? " " + size : ""}" role="img" aria-label="${def.label}"></span>`;
  return html`<span class="glyph ${def.cls}" role="img" aria-label="${def.label}">${icon(def.icon, size)}</span>`;
}

export function deviceDot(device) {
  const s = DEVICE_STATE[device.status] || { label: device.status, dot: "idle" };
  return html`<span class="dot dot-${s.dot}${s.pulse ? " pulse" : ""}" role="img" aria-label="${s.label}" title="${s.label}"></span>`;
}

export function offlineNote() {
  return state.conn === "offline" ? html`<div class="stale-note" role="status">Disconnected. Showing the last data received.</div>` : "";
}

