const CSI = /\u001b\[[0-?]*[ -/]*[@-~]/g;
const OSC = /\u001b\][^\u0007\u001b]*(?:\u0007|\u001b\\)/g;
const CONTROL = /[\u0000-\u0008\u000b-\u001f\u007f]/g;

/** Removes ANSI escape sequences. */
export function stripAnsi(text: string): string {
  return text.replace(OSC, "").replace(CSI, "");
}

/**
 * Text that came from the server (job output, error messages, names) is never written to the terminal
 * with its escape sequences: a build log must not be able to move the cursor or retitle the window.
 */
export function sanitize(text: string): string {
  return stripAnsi(text).replace(CONTROL, "");
}

/** One line: newlines and runs of whitespace collapse to a single space. */
export function oneLine(text: string): string {
  return sanitize(text).replace(/\s+/g, " ").trim();
}

/** Number of characters as displayed (ANSI ignored, code points counted once). */
export function visibleLength(text: string): number {
  return [...stripAnsi(text)].length;
}

export function truncate(text: string, max: number): string {
  const chars = [...text];
  if (chars.length <= max) return text;
  if (max <= 1) return chars.slice(0, Math.max(0, max)).join("");
  return `${chars.slice(0, max - 1).join("")}…`;
}

export function shortId(id: string): string {
  return id.slice(0, 8);
}

export function plural(count: number, singular: string, pluralForm = `${singular}s`): string {
  return `${count} ${count === 1 ? singular : pluralForm}`;
}

/**
 * Compact duration: `850ms`, `1.2s`, `42s`, `2m 05s`, `1h 03m`. Missing or nonsensical input gives an en dash.
 */
export function formatDuration(ms: number | undefined | null): string {
  if (ms === undefined || ms === null || !Number.isFinite(ms) || ms < 0) return "–";
  if (ms < 1000) return `${Math.round(ms)}ms`;
  const tenths = Math.round(ms / 100) / 10;
  if (tenths < 10) return `${tenths.toFixed(1)}s`;
  const totalSeconds = Math.round(ms / 1000);
  if (totalSeconds < 60) return `${totalSeconds}s`;
  const minutes = Math.floor(totalSeconds / 60);
  if (minutes < 60) return `${minutes}m ${String(totalSeconds % 60).padStart(2, "0")}s`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h ${String(minutes % 60).padStart(2, "0")}m`;
}

export function formatSeconds(seconds: number | undefined | null): string {
  return seconds === undefined || seconds === null ? "–" : formatDuration(seconds * 1000);
}

/** "3m ago", "just now", "2d ago". */
export function timeAgo(iso: string | undefined, now: number = Date.now()): string {
  if (!iso) return "–";
  const then = Date.parse(iso);
  if (Number.isNaN(then)) return "–";
  const seconds = Math.max(0, Math.round((now - then) / 1000));
  if (seconds < 5) return "just now";
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 48) return `${hours}h ago`;
  return `${Math.floor(hours / 24)}d ago`;
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}
