// Pop-up and context menus (role=menu). One menu at a time; opened from a button (anchor) or a pointer position.
// Item spec: { label, icon, checked, disabled, keys, run, keepOpen, header, separator, danger, sub }
// Keyboard: Up/Down/Home/End move, Enter/Space choose, Esc closes and returns focus, letters jump.

import { html, raw, keyLabel } from "../core/util.js";
import { icon } from "../core/icons.js";

let current = null;

export function closeMenu(reason = "close") {
  if (!current) return;
  const { root, backdrop, anchor, onClose, restore } = current;
  current = null;
  root.remove();
  backdrop.remove();
  if (anchor) anchor.setAttribute("aria-expanded", "false");
  if (onClose) onClose(reason);
  if (restore && reason !== "outside" && document.contains(restore)) restore.focus({ preventScroll: true });
}

export const isMenuOpen = () => !!current;

function itemHtml(item, i) {
  if (item.separator) return html`<div class="menu-sep" role="separator"></div>`;
  if (item.header) return html`<div class="menu-header" role="presentation">${item.header}</div>`;
  const role = item.checked !== undefined ? "menuitemcheckbox" : "menuitem";
  return html`<button type="button" class="mi${item.danger ? " mi-danger" : ""}" role="${role}" data-i="${i}"
      ${item.checked !== undefined ? raw(`aria-checked="${item.checked ? "true" : "false"}"`) : ""} ${item.disabled ? raw("disabled") : ""} tabindex="-1">
      <span class="mi-check">${item.checked ? icon("checkmark", "ic-12") : ""}</span>
      ${item.icon ? html`<span class="mi-icon">${icon(item.icon, "ic-14")}</span>` : ""}
      <span class="mi-label">${item.label}</span>
      ${item.sub ? html`<span class="mi-sub">${item.sub}</span>` : ""}
      ${item.keys ? html`<span class="mi-kbd">${keyLabel(item.keys)}</span>` : ""}
    </button>`;
}

/**
 * Opens a menu. `anchor` (an element) places it under that element; `point` ({x,y}) at the pointer.
 * Returns { update(items), close() }.
 */
export function openMenu({ items, anchor, point, align = "start", minWidth = 0, className = "", onClose, label = "Menu", focusFirst = false }) {
  closeMenu("replace");
  const layer = document.getElementById("menu-layer") || document.body;
  const backdrop = document.createElement("div");
  backdrop.className = "menu-backdrop";
  const root = document.createElement("div");
  root.className = `menu glass glass-menu glass-frosted ${className}`.trim();
  root.dataset.glassShape = "round";
  root.dataset.glassRadius = "12";
  root.setAttribute("role", "menu");
  root.setAttribute("aria-label", label);
  root.tabIndex = -1;
  layer.append(backdrop, root);
  if (anchor) anchor.setAttribute("aria-expanded", "true");

  const state = { root, backdrop, anchor, onClose, restore: anchor || document.activeElement, items };
  current = state;

  const render = () => {
    root.innerHTML = state.items.map((item, i) => itemHtml(item, i)).join("");
  };
  render();
  if (minWidth) root.style.minWidth = `${minWidth}px`;

  const place = () => {
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const rect = root.getBoundingClientRect();
    let x;
    let y;
    if (anchor) {
      const a = anchor.getBoundingClientRect();
      x = align === "end" ? a.right - rect.width : a.left;
      y = a.bottom + 4;
      if (y + rect.height > vh - 8) y = Math.max(8, a.top - rect.height - 4);
    } else {
      x = point.x;
      y = point.y;
      if (x + rect.width > vw - 8) x = point.x - rect.width;
      if (y + rect.height > vh - 8) y = Math.max(8, point.y - rect.height);
    }
    x = Math.min(Math.max(8, x), Math.max(8, vw - rect.width - 8));
    root.style.left = `${Math.round(x)}px`;
    root.style.top = `${Math.round(y)}px`;
    root.style.maxHeight = `${vh - 16}px`;
  };
  place();

  const enabledButtons = () => Array.from(root.querySelectorAll(".mi:not([disabled])"));
  const focusAt = (index) => {
    const list = enabledButtons();
    if (!list.length) return;
    list[(index + list.length) % list.length].focus({ preventScroll: false });
  };

  const choose = (button) => {
    const item = state.items[Number(button.dataset.i)];
    if (!item || item.disabled) return;
    if (item.keepOpen) {
      if (item.checked !== undefined) item.checked = !item.checked;
      if (item.run) item.run(item);
      const at = Number(button.dataset.i);
      render();
      root.querySelector(`.mi[data-i="${at}"]`)?.focus();
      place();
      return;
    }
    closeMenu("select");
    if (item.run) item.run(item);
  };

  root.addEventListener("click", (event) => {
    const button = event.target.closest(".mi");
    if (button) choose(button);
  });

  let typed = "";
  let typedAt = 0;
  root.addEventListener("keydown", (event) => {
    const list = enabledButtons();
    const at = list.indexOf(document.activeElement);
    switch (event.key) {
      case "ArrowDown": event.preventDefault(); focusAt(at + 1); break;
      case "ArrowUp": event.preventDefault(); focusAt(at === -1 ? -1 : at - 1); break;
      case "Home": event.preventDefault(); focusAt(0); break;
      case "End": event.preventDefault(); focusAt(-1); break;
      case "Escape": event.preventDefault(); event.stopPropagation(); closeMenu("escape"); break;
      case "Tab": event.preventDefault(); closeMenu("tab"); break;
      case "Enter":
      case " ":
        if (document.activeElement?.classList.contains("mi")) { event.preventDefault(); choose(document.activeElement); }
        break;
      default:
        if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
          const now = Date.now();
          typed = now - typedAt > 700 ? event.key.toLowerCase() : typed + event.key.toLowerCase();
          typedAt = now;
          const hit = list.find((b) => b.textContent.trim().toLowerCase().startsWith(typed));
          if (hit) hit.focus();
        }
    }
  });
  backdrop.addEventListener("pointerdown", (event) => { event.preventDefault(); closeMenu("outside"); });
  backdrop.addEventListener("contextmenu", (event) => { event.preventDefault(); closeMenu("outside"); });

  if (focusFirst) focusAt(0);
  else root.focus({ preventScroll: true });

  return {
    update(next) { state.items = next; render(); place(); },
    close: () => closeMenu("close")
  };
}

/** Menus are positioned once, so a resize closes them. */
window.addEventListener("resize", () => closeMenu("resize"));
