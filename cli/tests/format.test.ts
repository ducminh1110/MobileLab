import test from "node:test";
import assert from "node:assert/strict";
import { formatBytes, formatDuration, formatSeconds, oneLine, plural, sanitize, shortId, stripAnsi, timeAgo, truncate, visibleLength } from "../src/utils/format";

test("formatDuration picks a readable unit", () => {
  assert.equal(formatDuration(0), "0ms");
  assert.equal(formatDuration(850), "850ms");
  assert.equal(formatDuration(999.4), "999ms");
  assert.equal(formatDuration(1000), "1.0s");
  assert.equal(formatDuration(1234), "1.2s");
  assert.equal(formatDuration(9949), "9.9s");
  assert.equal(formatDuration(9960), "10s", "rounding up to 10.0s switches to whole seconds");
  assert.equal(formatDuration(42_000), "42s");
  assert.equal(formatDuration(59_400), "59s");
  assert.equal(formatDuration(59_600), "1m 00s", "59.6s must not print as 60s");
  assert.equal(formatDuration(125_000), "2m 05s");
  assert.equal(formatDuration(3_599_000), "59m 59s");
  assert.equal(formatDuration(3_600_000), "1h 00m");
  assert.equal(formatDuration(3_780_000), "1h 03m");
});

test("formatDuration shows an en dash for missing or nonsensical input", () => {
  assert.equal(formatDuration(undefined), "–");
  assert.equal(formatDuration(null), "–");
  assert.equal(formatDuration(-5), "–");
  assert.equal(formatDuration(Number.NaN), "–");
  assert.equal(formatDuration(Number.POSITIVE_INFINITY), "–");
});

test("formatSeconds converts from seconds", () => {
  assert.equal(formatSeconds(0.412), "412ms");
  assert.equal(formatSeconds(90), "1m 30s");
  assert.equal(formatSeconds(undefined), "–");
});

test("timeAgo", () => {
  const now = Date.parse("2026-01-01T12:00:00Z");
  const ago = (seconds: number) => timeAgo(new Date(now - seconds * 1000).toISOString(), now);
  assert.equal(ago(1), "just now");
  assert.equal(ago(30), "30s ago");
  assert.equal(ago(5 * 60), "5m ago");
  assert.equal(ago(3 * 3600), "3h ago");
  assert.equal(ago(3 * 86400), "3d ago");
  assert.equal(timeAgo(undefined, now), "–");
  assert.equal(timeAgo("not a date", now), "–");
});

test("small helpers", () => {
  assert.equal(shortId("6c1f0b2e-1111-2222-3333-444444444444"), "6c1f0b2e");
  assert.equal(plural(1, "job"), "1 job");
  assert.equal(plural(2, "job"), "2 jobs");
  assert.equal(plural(0, "retry", "retries"), "0 retries");
  assert.equal(truncate("abcdef", 4), "abc…");
  assert.equal(truncate("abcd", 4), "abcd");
  assert.equal(truncate("abc", 1), "a");
  assert.equal(formatBytes(512), "512 B");
  assert.equal(formatBytes(2048), "2.0 KB");
  assert.equal(formatBytes(3 * 1024 * 1024), "3.0 MB");
});

test("text from the server never carries terminal escape sequences", () => {
  const hostile = "\u001b[31mred\u001b[0m \u001b]0;pwned\u0007title \u001b[2J\u0007bell";
  assert.equal(stripAnsi("\u001b[1mbold\u001b[22m"), "bold");
  assert.equal(sanitize(hostile), "red title bell");
  assert.equal(oneLine("a\n  b\t\tc\r\n"), "a b c");
  assert.equal(visibleLength("\u001b[32mok\u001b[39m"), 2);
  assert.equal(visibleLength("✔ ok"), 4);
});
