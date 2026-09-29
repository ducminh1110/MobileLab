// A tiny keyed DOM patcher. Panes render to an HTML string; `patch` diffs that string against the live DOM instead of
// replacing it, so focus, scroll positions, CSS animations and text selection survive every data refresh. When the
// produced markup is identical to the previous render nothing is touched at all.
//
// Conventions in markup:
//   data-key="..."      identity for reordering (rows, sections); focus is restored by this key
//   data-v="--w:34%"    CSS custom properties applied through the CSSOM (the CSP forbids style="" attributes)
//   data-morph="static" children are left alone after the first render (the log viewer host, text inputs)

function keyOf(node) {
  return node.nodeType === 1 ? node.getAttribute("data-key") : null;
}

export function applyVars(el) {
  const spec = el.getAttribute("data-v") || "";
  if (el.__v === spec) return;
  const prev = el.__vNames || [];
  const next = [];
  for (const decl of spec.split(";")) {
    const at = decl.indexOf(":");
    if (at === -1) continue;
    const name = decl.slice(0, at).trim();
    const value = decl.slice(at + 1).trim();
    if (!name) continue;
    el.style.setProperty(name, value);
    next.push(name);
  }
  for (const name of prev) if (!next.includes(name)) el.style.removeProperty(name);
  el.__v = spec;
  el.__vNames = next;
}

export function applyVarsDeep(root) {
  if (root.nodeType === 1 && root.hasAttribute("data-v")) applyVars(root);
  if (root.querySelectorAll) for (const el of root.querySelectorAll("[data-v]")) applyVars(el);
}

function syncAttributes(from, to) {
  for (const attr of Array.from(from.attributes)) {
    if (!to.hasAttribute(attr.name)) from.removeAttribute(attr.name);
  }
  for (const attr of Array.from(to.attributes)) {
    if (from.getAttribute(attr.name) !== attr.value) from.setAttribute(attr.name, attr.value);
  }
}

function morphNode(from, to) {
  if (from.nodeType !== 1) {
    if (from.nodeValue !== to.nodeValue) from.nodeValue = to.nodeValue;
    return;
  }
  const isStatic = from.getAttribute("data-morph") === "static" && from.__seeded;
  syncAttributes(from, to);
  if (from.hasAttribute("data-v")) applyVars(from);

  const tag = from.nodeName;
  if (tag === "INPUT") {
    if (from !== document.activeElement && from.type !== "file") {
      if (from.type === "checkbox" || from.type === "radio") from.checked = to.hasAttribute("checked");
      else if (from.value !== (to.getAttribute("value") ?? "")) from.value = to.getAttribute("value") ?? "";
    } else if (from.type === "checkbox" || from.type === "radio") {
      from.checked = to.hasAttribute("checked");
    }
    return;
  }
  if (tag === "TEXTAREA") {
    if (from !== document.activeElement && from.value !== to.textContent) from.value = to.textContent;
    return;
  }
  if (tag === "SELECT") {
    if (from !== document.activeElement) {
      morphChildren(from, to);
      const chosen = Array.from(to.querySelectorAll("option")).findIndex((o) => o.hasAttribute("selected"));
      if (chosen >= 0) from.selectedIndex = chosen;
    }
    return;
  }
  if (isStatic) return;
  from.__seeded = true;
  morphChildren(from, to);
}

function morphChildren(parent, source) {
  const oldKids = Array.from(parent.childNodes);
  const keyed = new Map();
  for (const node of oldKids) {
    const key = keyOf(node);
    if (key != null) keyed.set(key, node);
  }
  const used = new Set();
  let ref = parent.firstChild;

  for (const next of Array.from(source.childNodes)) {
    const key = keyOf(next);
    let match = null;

    if (key != null) {
      const candidate = keyed.get(key);
      if (candidate && !used.has(candidate) && candidate.nodeName === next.nodeName) match = candidate;
    } else {
      while (ref && used.has(ref)) ref = ref.nextSibling;
      if (ref && keyOf(ref) == null && ref.nodeType === next.nodeType && ref.nodeName === next.nodeName) match = ref;
    }

    if (match) {
      used.add(match);
      if (match === ref) ref = ref.nextSibling;
      else parent.insertBefore(match, ref);
      morphNode(match, next);
    } else {
      const fresh = next.cloneNode(true);
      parent.insertBefore(fresh, ref);
      used.add(fresh);
      if (fresh.nodeType === 1) {
        applyVarsDeep(fresh);
        fresh.__seeded = true;
        for (const inner of fresh.querySelectorAll("[data-morph]")) inner.__seeded = true;
      }
    }
  }

  for (const node of oldKids) if (!used.has(node) && node.parentNode === parent) parent.removeChild(node);
}

export function morph(container, markup) {
  const template = document.createElement("template");
  template.innerHTML = markup;
  morphChildren(container, template.content);
}

const selectorFor = (key) => `[data-key="${CSS.escape(key)}"]`;

/** Renders `markup` into `el` unless it is unchanged; keeps keyboard focus on the same keyed element. */
export function patch(el, markup) {
  const text = String(markup);
  if (el.__html === text) return false;
  const active = document.activeElement;
  const inside = active && active !== document.body && el.contains(active);
  const focusKey = inside ? active.closest("[data-key]")?.getAttribute("data-key") : null;
  const selection = inside && (active.tagName === "INPUT" || active.tagName === "TEXTAREA") ? [active.selectionStart, active.selectionEnd] : null;
  morph(el, text);
  el.__html = text;
  if (focusKey && !el.contains(document.activeElement)) {
    const target = el.querySelector(selectorFor(focusKey));
    if (target) {
      const focusable = target.matches("[tabindex], button, a, input, textarea, select") ? target : target.querySelector("[tabindex], button, a, input, textarea, select");
      (focusable || target).focus({ preventScroll: true });
      if (selection && focusable && focusable.setSelectionRange) {
        try { focusable.setSelectionRange(selection[0], selection[1]); } catch { /* not a text field */ }
      }
    }
  }
  return true;
}
