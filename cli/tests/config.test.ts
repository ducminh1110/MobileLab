import test from "node:test";
import assert from "node:assert/strict";
import { DEFAULT_API_URL, normalizeApiUrl, resolveConfig } from "../src/config";
import { UsageError } from "../src/errors";
import { CliIO } from "../src/io";

function io(env: NodeJS.ProcessEnv = {}, stdoutTty = false, stderrTty = false): CliIO {
  return { stdout: { write: () => {}, isTTY: stdoutTty }, stderr: { write: () => {}, isTTY: stderrTty }, env, cwd: "/" };
}

test("normalizeApiUrl accepts bare hosts, drops trailing slashes, keeps a path prefix", () => {
  assert.equal(normalizeApiUrl("http://127.0.0.1:4000/"), "http://127.0.0.1:4000");
  assert.equal(normalizeApiUrl("localhost:4000"), "http://localhost:4000");
  assert.equal(normalizeApiUrl("https://lab.example.com/mobilelab//"), "https://lab.example.com/mobilelab");
  assert.equal(normalizeApiUrl("  http://lab:1?x=1#y "), "http://lab:1");
});

test("normalizeApiUrl rejects things that are not http(s) URLs", () => {
  assert.throws(() => normalizeApiUrl("ftp://lab"), UsageError);
  assert.throws(() => normalizeApiUrl("http://"), UsageError);
  assert.throws(() => normalizeApiUrl("http://exa mple.com"), UsageError);
});

test("the API URL comes from the flag, then the environment, then the default", () => {
  assert.equal(resolveConfig({}, io()).apiUrl, DEFAULT_API_URL);
  assert.equal(resolveConfig({}, io({ IOSLAB_API_URL: "http://env:1" })).apiUrl, "http://env:1");
  assert.equal(resolveConfig({ api: "http://flag:2" }, io({ IOSLAB_API_URL: "http://env:1" })).apiUrl, "http://flag:2");
  assert.equal(resolveConfig({}, io({ IOSLAB_API_URL: "" })).apiUrl, DEFAULT_API_URL, "an empty variable counts as unset");
});

test("the token comes from the flag, then the environment", () => {
  assert.equal(resolveConfig({}, io()).token, undefined);
  assert.equal(resolveConfig({}, io({ IOSLAB_API_TOKEN: "env" })).token, "env");
  assert.equal(resolveConfig({ token: "flag" }, io({ IOSLAB_API_TOKEN: "env" })).token, "flag");
});

test("a token with surrounding whitespace is trimmed; one that cannot be an HTTP header value is rejected", () => {
  assert.equal(resolveConfig({}, io({ IOSLAB_API_TOKEN: " abc\n" })).token, "abc");
  assert.equal(resolveConfig({}, io({ IOSLAB_API_TOKEN: "   " })).token, undefined);
  assert.throws(() => resolveConfig({ token: "a b" }, io()), UsageError);
  assert.throws(() => resolveConfig({ token: "a\u0001b" }, io()), UsageError);
  assert.throws(() => resolveConfig({ token: "tökén" }, io()), UsageError);
});

test("colors and spinners need a terminal, and respect NO_COLOR and --no-color", () => {
  const pipe = resolveConfig({}, io());
  assert.deepEqual([pipe.color, pipe.interactive], [false, false]);

  const tty = resolveConfig({}, io({}, true, true));
  assert.deepEqual([tty.color, tty.interactive], [true, true]);

  assert.equal(resolveConfig({}, io({ NO_COLOR: "1" }, true, true)).color, false);
  assert.equal(resolveConfig({}, io({ NO_COLOR: "" }, true, true)).color, true, "an empty NO_COLOR is ignored, as the convention says");
  assert.equal(resolveConfig({ color: false }, io({}, true, true)).color, false);
  assert.equal(resolveConfig({}, io({ TERM: "dumb" }, true, true)).color, false);
  assert.equal(resolveConfig({}, io({ TERM: "dumb" }, true, true)).interactive, false);

  // stdout piped but stderr on a terminal: still no spinner or ANSI
  const piped = resolveConfig({}, io({}, false, true));
  assert.deepEqual([piped.color, piped.interactive], [false, false]);
  // stdout on a terminal, stderr redirected: colors yes, spinner (which draws on stderr) no
  const errRedirected = resolveConfig({}, io({}, true, false));
  assert.deepEqual([errRedirected.color, errRedirected.interactive], [true, false]);

  assert.equal(resolveConfig({ json: true }, io({}, true, true)).interactive, false, "--json never draws a spinner");

  // A terminal inside CI keeps colors (the log viewer may render them) but gets plain progress lines.
  assert.equal(resolveConfig({}, io({ CI: "true" }, true, true)).interactive, false);
  assert.equal(resolveConfig({}, io({ CI: "true" }, true, true)).color, true);
  assert.equal(resolveConfig({}, io({ CI: "false" }, true, true)).interactive, true);
});
