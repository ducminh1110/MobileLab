// Toast queue. The DOM part lives in ui/toasts.js; anything can call notify() without importing UI code.

const listeners = new Set();
let seq = 0;

export function onToast(fn) { listeners.add(fn); return () => listeners.delete(fn); }

/** kind: "info" | "success" | "error". Errors stay longer and use role=alert. */
export function notify(message, { kind = "info", ttl, detail = "" } = {}) {
  const toast = { id: ++seq, message: String(message), kind, detail, ttl: ttl ?? (kind === "error" ? 9000 : 3800) };
  for (const fn of listeners) fn(toast);
  return toast.id;
}

export const notifyError = (error, prefix = "") => {
  const message = error && error.message ? error.message : String(error);
  return notify(prefix ? `${prefix}: ${message}` : message, { kind: "error" });
};
