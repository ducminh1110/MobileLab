// Event delegation. Markup carries data-action="name" (plus data-* arguments); modules register handlers here.
// One listener per event type on the document keeps rendering free of per-element wiring, and works with the
// morph patcher (elements come and go, the listener stays).

const clickHandlers = new Map();
const contextHandlers = new Map();
const inputHandlers = new Map();
const keyHandlers = [];

export function action(name, fn) { clickHandlers.set(name, fn); }
export function contextMenu(kind, fn) { contextHandlers.set(kind, fn); }
/** data-field="name" inputs: fn({ el, name, value, event }) on input; `change` events too. */
export function field(scope, fn) { inputHandlers.set(scope, fn); }
/** Global key handlers run first-come; return true to stop. */
export function onKey(fn) { keyHandlers.push(fn); }

const disabled = (el) => el.disabled || el.getAttribute("aria-disabled") === "true";

export function initDispatch() {
  document.addEventListener("click", (event) => {
    const el = event.target.closest?.("[data-action]");
    if (!el || disabled(el)) return;
    const fn = clickHandlers.get(el.dataset.action);
    if (!fn) return;
    if (event.button !== 0) return;
    fn({ el, arg: el.dataset.arg, event });
  });

  document.addEventListener("contextmenu", (event) => {
    const el = event.target.closest?.("[data-ctx]");
    if (!el) return;
    const fn = contextHandlers.get(el.dataset.ctx);
    if (!fn) return;
    event.preventDefault();
    fn({ el, point: { x: event.clientX, y: event.clientY }, event });
  });

  const onField = (event) => {
    const el = event.target.closest?.("[data-field]");
    if (!el) return;
    const scope = el.dataset.scope;
    const fn = inputHandlers.get(scope);
    if (!fn) return;
    const value = el.type === "checkbox" ? el.checked : el.value;
    fn({ el, name: el.dataset.field, value, event, kind: event.type });
  };
  document.addEventListener("input", onField);
  document.addEventListener("change", onField);

  document.addEventListener("keydown", (event) => {
    // Context menu from the keyboard: the Menu key or Shift+F10 on a focused item.
    if (event.key === "ContextMenu" || (event.key === "F10" && event.shiftKey)) {
      const el = document.activeElement?.closest?.("[data-ctx]");
      const fn = el && contextHandlers.get(el.dataset.ctx);
      if (fn) {
        event.preventDefault();
        const r = el.getBoundingClientRect();
        fn({ el, point: { x: r.left + 24, y: r.bottom - 4 }, event, keyboard: true });
        return;
      }
    }
    // Enter / Space activates non-button elements that carry an action (role=button, role=tab, rows).
    if ((event.key === "Enter" || event.key === " ") && !event.ctrlKey && !event.metaKey && !event.altKey) {
      const el = event.target.closest?.("[data-action]");
      if (el && el === event.target && el.tagName !== "BUTTON" && el.tagName !== "A" && el.tagName !== "INPUT" && !el.hasAttribute("data-native-keys") && !disabled(el)) {
        const fn = clickHandlers.get(el.dataset.action);
        if (fn) {
          event.preventDefault();
          fn({ el, arg: el.dataset.arg, event, keyboard: true });
          return;
        }
      }
    }
    for (const fn of keyHandlers) if (fn(event)) return;
  }, true);
}
