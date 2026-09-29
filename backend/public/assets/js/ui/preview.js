// Simulator preview: the live screenshot inside an iPhone-shaped frame (Xcode's canvas). While the selected
// device is up, GET /devices/:id/screenshot is refreshed every 3 s (as an authenticated blob, so it also works
// with an API token) and swapped in only once the new image has decoded, so it never flickers.

import { html, raw, relTime } from "../core/util.js";
import { icon } from "../core/icons.js";
import { api } from "../core/api.js";
import { state, invalidate, deviceById } from "../core/state.js";
import { deviceModel, deviceRuntime, deviceState, isBooted, isTablet } from "../core/models.js";

const REFRESH_MS = 3000;
const shots = new Map(); // deviceId -> { url, at, error, loading }
let timer = 0;
let current = null;

const ZOOMS = [0.5, 0.75, 1, 1.25, 1.5, 2];

async function refresh(id, { manual = false } = {}) {
  const entry = shots.get(id) || { url: "", at: 0, error: "", loading: false };
  if (entry.loading) return;
  entry.loading = true;
  shots.set(id, entry);
  try {
    const blob = await api.blob(`/devices/${encodeURIComponent(id)}/screenshot`);
    const url = URL.createObjectURL(blob);
    await new Promise((resolve) => {
      const img = new Image();
      img.onload = img.onerror = resolve;
      img.src = url;
    });
    const old = entry.url;
    entry.url = url;
    entry.at = Date.now();
    entry.error = "";
    if (old) setTimeout(() => URL.revokeObjectURL(old), 2000);
  } catch (error) {
    entry.error = error.message;
    if (manual) invalidate("editor");
  } finally {
    entry.loading = false;
    invalidate("editor");
  }
}

export function refreshShot(id) { void refresh(id, { manual: true }); }

/** Starts or stops the refresh loop to match what is on screen. Called after every editor render. */
export function syncPreview() {
  const device = state.sel.kind === "device" ? deviceById(state.sel.id) : null;
  const wanted = device && device.type === "simulator" && isBooted(device) && state.conn === "online" ? device.id : null;
  if (wanted === current) return;
  clearInterval(timer);
  timer = 0;
  current = wanted;
  if (!wanted) return;
  void refresh(wanted);
  timer = setInterval(() => {
    if (document.visibilityState === "visible" && state.conn === "online") void refresh(wanted);
  }, REFRESH_MS);
}

export function zoomStep(delta) {
  const now = state.ui.zoom ?? "fit";
  const at = now === "fit" ? ZOOMS.indexOf(1) : Math.max(0, ZOOMS.indexOf(now));
  state.ui.zoom = ZOOMS[Math.min(ZOOMS.length - 1, Math.max(0, at + delta))];
  invalidate("editor");
}

export function zoomSet(value) {
  state.ui.zoom = value;
  invalidate("editor");
}

export function previewHtml(device) {
  const st = deviceState(device);
  const entry = shots.get(device.id);
  const up = isBooted(device);
  const tablet = isTablet(device);
  const zoom = state.ui.zoom ?? "fit";
  const canvasMode = zoom === "fit" ? "fit" : "scaled";
  const vm = device.type === "vm";

  let screen;
  if (vm) {
    screen = html`<div class="scr-msg">${icon("cpu", "ic-32")}<strong>Simulated VM</strong><p>This entry exercises the VM API. It has no screen and cannot run tests.</p></div>`;
  } else if (up && entry?.url) {
    screen = html`<img class="scr-img" data-key="shot" src="${entry.url}" alt="Live screenshot of ${device.name}" draggable="false">`;
  } else if (up && entry?.error) {
    screen = html`<div class="scr-msg"><span class="c-fail">${icon("exclamationmark.triangle", "ic-32")}</span><strong>No screenshot</strong><p class="err-inline" role="alert">${entry.error}</p><button type="button" class="btn" data-action="shot-refresh" data-id="${device.id}">Try Again</button></div>`;
  } else if (up) {
    screen = html`<div class="scr-msg"><span class="spin c-accent"></span><p>Capturing the screen…</p></div>`;
  } else if (device.status === "booting" || device.status === "shutting_down") {
    screen = html`<div class="scr-msg"><span class="spin c-warn"></span><strong>${st.label}…</strong></div>`;
  } else if (device.status === "error") {
    screen = html`<div class="scr-msg c-fail">${icon("exclamationmark.triangle", "ic-32")}<strong>${device.name} has an error</strong><p class="err-inline" role="alert">${device.lastError || "The simulator reported an error."}</p><button type="button" class="btn" data-action="device-boot" data-id="${device.id}">Boot</button></div>`;
  } else {
    screen = html`<div class="scr-msg">${icon("iphone", "ic-32")}<strong>${st.label}</strong><p>Boot this simulator to see its screen here.</p><button type="button" class="btn btn-primary" data-action="device-boot" data-id="${device.id}">Boot</button></div>`;
  }

  const zoomLabel = zoom === "fit" ? "Fit" : `${Math.round(zoom * 100)}%`;
  const bootable = !up && device.status !== "booting" && device.status !== "shutting_down" && device.type === "simulator";
  return html`
    <div class="canvas" data-key="canvas">
      <div class="canvas-head">
        <span class="pin">${icon("eye", "ic-14")}</span>
        <span class="canvas-pill">${icon("eye", "ic-12")}<span>${device.name}</span></span>
        <span class="canvas-meta">${deviceRuntime(device)} · ${deviceModel(device)}${entry?.at && up ? html` · updated <time data-rel="${new Date(entry.at).toISOString()}">${relTime(new Date(entry.at).toISOString())}</time>` : ""}</span>
      </div>
      <div class="canvas-stage" data-mode="${canvasMode}">
        <div class="phone${tablet ? " tablet" : ""}${up || vm ? "" : " off"}" data-v="--zoom:${zoom === "fit" ? 1 : zoom}">
          <div class="phone-screen">${screen}</div>
          ${tablet ? "" : html`<span class="island"></span>`}
        </div>
      </div>
      <div class="canvas-bar">
        <div class="cb-group">
          <button type="button" class="cb-btn cb-accent" data-action="${bootable ? "device-boot" : "device-run"}" data-id="${device.id}" ${device.canRunTests || bootable ? "" : raw("disabled")} title="${bootable ? "Boot" : "Run Tests Here"}" aria-label="${bootable ? "Boot" : "Run tests here"}">${icon("play.fill", "ic-14")}</button>
          <button type="button" class="cb-btn" data-action="device-shot" data-id="${device.id}" ${up && !vm ? "" : raw("disabled")} title="Save Screenshot" aria-label="Save screenshot">${icon("camera", "ic-14")}</button>
          <button type="button" class="cb-btn" data-action="shot-refresh" data-id="${device.id}" ${up && !vm ? "" : raw("disabled")} title="Refresh Now" aria-label="Refresh screenshot">${icon("arrow.clockwise", "ic-14")}</button>
        </div>
        <div class="cb-group">
          <button type="button" class="cb-btn" data-action="zoom-out" title="Zoom Out" aria-label="Zoom out">${icon("minus.magnifyingglass", "ic-14")}</button>
          <button type="button" class="cb-btn" data-action="zoom-in" title="Zoom In" aria-label="Zoom in">${icon("plus.magnifyingglass", "ic-14")}</button>
          <button type="button" class="cb-btn" data-action="zoom-actual" title="Actual Size" aria-label="Actual size">${icon("equal.magnifyingglass", "ic-14")}</button>
          <button type="button" class="cb-btn" data-action="zoom-fit" title="Fit to Canvas" aria-label="Fit to canvas" aria-pressed="${zoom === "fit" ? "true" : "false"}">${icon("fit.magnifyingglass", "ic-14")}</button>
          <span class="cb-zoom" aria-live="polite">${zoomLabel}</span>
        </div>
      </div>
    </div>`;
}
