// REST client for the MobileLab backend (same origin). Every failure becomes an ApiError whose message is what the
// server said, so the UI can show it next to the thing that failed.

import { state, invalidate } from "./state.js";
import { storage } from "./util.js";

const TOKEN_KEY = "mobilelab.token";

export const token = {
  get: () => storage.getRaw(TOKEN_KEY, ""),
  set: (value) => storage.setRaw(TOKEN_KEY, value),
  clear: () => storage.remove(TOKEN_KEY)
};

export class ApiError extends Error {
  constructor(message, { status = 0, code = "", network = false } = {}) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
    this.network = network;
  }
}

let authHandler = null;
export function onAuthRequired(fn) { authHandler = fn; }

function headers(extra = {}) {
  const h = { ...extra };
  const t = token.get();
  if (t) h.authorization = `Bearer ${t}`;
  return h;
}

async function errorFrom(res) {
  let message = `${res.status} ${res.statusText || "Request failed"}`;
  let code = "";
  try {
    const text = await res.text();
    try {
      const body = JSON.parse(text);
      if (body && body.message) message = body.message;
      else if (body && body.error) message = String(body.error);
      code = body?.error || "";
    } catch {
      if (text && text.length < 300) message = text;
    }
  } catch { /* keep the status line */ }
  return new ApiError(message, { status: res.status, code });
}

async function request(method, path, { body, signal, as = "json", quiet401 = false } = {}) {
  const init = { method, headers: headers({ accept: as === "json" ? "application/json" : "*/*" }), signal, cache: "no-store" };
  if (body !== undefined) {
    init.headers["content-type"] = "application/json";
    init.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(path, init);
  } catch (error) {
    if (error && error.name === "AbortError") throw error;
    throw new ApiError("Cannot reach the MobileLab backend.", { network: true });
  }
  if (res.status === 401) {
    const err = await errorFrom(res);
    if (!quiet401) {
      state.auth = "required";
      invalidate("toolbar");
      if (authHandler) authHandler(err.message);
    }
    throw err;
  }
  if (!res.ok) throw await errorFrom(res);
  if (as === "blob") return res.blob();
  if (as === "text") return res.text();
  if (res.status === 204) return null;
  const text = await res.text();
  return text ? JSON.parse(text) : null;
}

export const api = {
  get: (path, opts) => request("GET", path, opts),
  post: (path, body, opts) => request("POST", path, { ...opts, body: body ?? {} }),
  del: (path, opts) => request("DELETE", path, opts),
  blob: (path, opts) => request("GET", path, { ...opts, as: "blob" }),
  text: (path, opts) => request("GET", path, { ...opts, as: "text" })
};

export function wsUrl(path, params = {}) {
  const url = new URL(path, location.href);
  url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
  for (const [key, value] of Object.entries(params)) if (value !== undefined && value !== null && value !== "") url.searchParams.set(key, String(value));
  const t = token.get();
  if (t) url.searchParams.set("token", t);
  return url.toString();
}

/** Downloads an authenticated resource through fetch (a plain link cannot send the token header). */
export async function download(path, filename) {
  const blob = await api.blob(path);
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.className = "sr-only";
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}
