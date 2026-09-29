// Transient messages. Errors stay longer and are announced as alerts; everything is dismissible.

import { html } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { onToast } from "../core/notify.js";
import { action } from "./dispatch.js";

const MAX = 4;
let toasts = [];

function render() {
  const host = document.getElementById("toasts");
  patch(host, html`${toasts.map((t) => html`<div class="toast toast-${t.kind}" role="${t.kind === "error" ? "alert" : "status"}" data-key="t-${t.id}">
    <span class="toast-ic">${icon(t.kind === "error" ? "exclamationmark.triangle.fill" : t.kind === "success" ? "checkmark.circle.fill" : "info.circle.fill", "ic-16")}</span>
    <span class="toast-msg">${t.message}</span>
    <button type="button" class="toast-x" data-action="toast-close" data-id="${t.id}" aria-label="Dismiss" title="Dismiss">${icon("xmark", "ic-12")}</button>
  </div>`)}`);
}

export function initToasts() {
  onToast((toast) => {
    toasts = [...toasts.filter((t) => t.message !== toast.message), toast].slice(-MAX);
    render();
    setTimeout(() => { toasts = toasts.filter((t) => t.id !== toast.id); render(); }, toast.ttl);
  });
  action("toast-close", ({ el }) => { toasts = toasts.filter((t) => String(t.id) !== el.dataset.id); render(); });
}
