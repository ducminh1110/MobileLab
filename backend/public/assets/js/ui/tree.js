// Trees as the WAI-ARIA tree pattern with a flat DOM (role=tree, role=treeitem with aria-level): roving tabindex,
// Up/Down/Home/End move focus, Right expands (or enters the first child), Left collapses (or goes to the parent),
// Enter opens and moves focus to the editor, Space opens and keeps focus, letters type-ahead.

import { html, raw, esc } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state, sameSel, setExpanded, invalidate } from "../core/state.js";

/**
 * Row spec: { nav, key, level, expandable, expanded, sel:{kind,id}, open:{kind,id,tab,caseName,line}, icon, label,
 *   secondary, trailing, ctx, title, dim, disabled, cls, hoverAction }
 */
export function rowHtml(row, tabStop) {
  const selected = row.sel && sameSel(state.sel, row.sel);
  const attrs = [];
  if (row.expandable) attrs.push(`aria-expanded="${row.expanded ? "true" : "false"}"`);
  if (row.open) {
    for (const [k, v] of Object.entries(row.open)) if (v !== undefined && v !== null) attrs.push(`data-${k.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase())}="${esc(v)}"`);
  }
  if (row.ctx) attrs.push(`data-ctx="${row.ctx}"`);
  if (row.disabled) attrs.push('aria-disabled="true"');
  return html`<div class="row lvl-${row.level}${row.cls ? " " + row.cls : ""}${row.dim ? " dim" : ""}" role="treeitem" aria-level="${row.level + 1}"
      aria-selected="${selected ? "true" : "false"}" data-key="${row.nav}:${row.key}" data-row-key="${row.key}" data-nav="${row.nav}"
      data-action="row" data-native-keys ${row.expandable ? raw('data-expandable="1"') : ""} ${raw(attrs.join(" "))}
      tabindex="${tabStop ? 0 : -1}"${row.title ? html` title="${row.title}"` : ""}>
      <span class="disc${row.expandable ? "" : " none"}" ${row.expandable ? raw('data-action="row-toggle"') : ""}>${row.expandable ? icon(row.expanded ? "chevron.down" : "chevron.right", "ic-10") : ""}</span>
      ${row.icon ? html`<span class="row-icon">${row.icon}</span>` : ""}
      <span class="row-label">${row.label}</span>${row.secondary ? html`<span class="row-secondary">${row.secondary}</span>` : ""}
      ${row.trailing ? html`<span class="row-trail">${row.trailing}</span>` : ""}
      ${row.hoverAction ? row.hoverAction : ""}
      ${row.extra ? row.extra : ""}
    </div>`;
}

/** Renders flat rows; exactly one gets tabindex=0 (the remembered focus, else the selection, else the first). */
export function treeHtml(rows, { nav, label }) {
  const remembered = state.ui.treeFocus[nav];
  let stop = rows.findIndex((r) => r.key === remembered && !r.disabled);
  if (stop === -1) stop = rows.findIndex((r) => r.sel && sameSel(state.sel, r.sel));
  if (stop === -1) stop = rows.findIndex((r) => !r.disabled);
  return html`<div class="tree" role="tree" aria-label="${label}" data-nav="${nav}">${rows.map((r, i) => rowHtml(r, i === stop))}</div>`;
}

export function toggleRow(nav, key, open) {
  setExpanded(nav, key, open);
  invalidate(nav === "vars" ? "debug" : "nav");
}

// ---------------------------------------------------------------- keyboard

const typeahead = { text: "", at: 0 };

function items(tree) { return Array.from(tree.querySelectorAll('[role="treeitem"]')); }

function focusItem(tree, el) {
  if (!el) return;
  for (const other of tree.querySelectorAll('[role="treeitem"][tabindex="0"]')) if (other !== el) other.tabIndex = -1;
  el.tabIndex = 0;
  el.focus({ preventScroll: false });
  el.scrollIntoView({ block: "nearest" });
}

export function initTreeKeys(onOpen, onToggle) {
  document.addEventListener("focusin", (event) => {
    const row = event.target.closest?.('[role="treeitem"]');
    if (!row) return;
    const tree = row.closest('[role="tree"]');
    if (!tree) return;
    state.ui.treeFocus[tree.dataset.nav] = row.dataset.rowKey;
    for (const other of tree.querySelectorAll('[role="treeitem"][tabindex="0"]')) if (other !== row) other.tabIndex = -1;
    row.tabIndex = 0;
  });

  document.addEventListener("keydown", (event) => {
    const row = event.target.closest?.('[role="treeitem"]');
    if (!row || event.target !== row) return;
    const tree = row.closest('[role="tree"]');
    if (!tree) return;
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const list = items(tree);
    const at = list.indexOf(row);
    const level = Number(row.getAttribute("aria-level"));
    const expandable = row.hasAttribute("aria-expanded");
    const expanded = row.getAttribute("aria-expanded") === "true";
    const nav = row.dataset.nav;
    const key = row.dataset.rowKey;

    switch (event.key) {
      case "ArrowDown": event.preventDefault(); focusItem(tree, list[Math.min(list.length - 1, at + 1)]); return;
      case "ArrowUp": event.preventDefault(); focusItem(tree, list[Math.max(0, at - 1)]); return;
      case "Home": event.preventDefault(); focusItem(tree, list[0]); return;
      case "End": event.preventDefault(); focusItem(tree, list[list.length - 1]); return;
      case "ArrowRight":
        event.preventDefault();
        if (expandable && !expanded) onToggle(nav, key, true);
        else if (expandable && expanded) {
          const next = list[at + 1];
          if (next && Number(next.getAttribute("aria-level")) > level) focusItem(tree, next);
        }
        return;
      case "ArrowLeft":
        event.preventDefault();
        if (expandable && expanded) onToggle(nav, key, false);
        else {
          for (let i = at - 1; i >= 0; i -= 1) {
            if (Number(list[i].getAttribute("aria-level")) < level) { focusItem(tree, list[i]); break; }
          }
        }
        return;
      case "Enter":
        event.preventDefault();
        if (row.dataset.kind) onOpen(row, { focusEditor: true });
        else if (expandable) onToggle(nav, key, !expanded);
        return;
      case " ":
        event.preventDefault();
        if (row.dataset.kind) onOpen(row, { focusEditor: false });
        else if (expandable) onToggle(nav, key, !expanded);
        return;
      case "*":
        event.preventDefault();
        for (const sibling of list) {
          if (Number(sibling.getAttribute("aria-level")) === level && sibling.hasAttribute("aria-expanded") && sibling.getAttribute("aria-expanded") === "false") onToggle(sibling.dataset.nav, sibling.dataset.rowKey, true);
        }
        return;
      default:
        if (event.key.length === 1 && /\S/.test(event.key)) {
          const now = Date.now();
          typeahead.text = now - typeahead.at > 700 ? event.key.toLowerCase() : typeahead.text + event.key.toLowerCase();
          typeahead.at = now;
          const start = typeahead.text.length === 1 ? at + 1 : at;
          for (let i = 0; i < list.length; i += 1) {
            const candidate = list[(start + i) % list.length];
            const label = candidate.querySelector(".row-label")?.textContent.trim().toLowerCase() || "";
            if (label.startsWith(typeahead.text)) { focusItem(tree, candidate); break; }
          }
        }
    }
  });
}
