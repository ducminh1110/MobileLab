import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { CommandError, ensureSuccess, RealCommandRunner } from "../../src/simulator/engine/commandRunner";

const runner = new RealCommandRunner();
const node = process.execPath;

test("captures output and exit code without throwing on failure", async () => {
  const ok = await runner.run(node, ["-e", "console.log('hi'); console.error('warn')"]);
  assert.equal(ok.code, 0);
  assert.equal(ok.stdout.trim(), "hi");
  assert.equal(ok.stderr.trim(), "warn");

  const bad = await runner.run(node, ["-e", "process.exit(65)"]);
  assert.equal(bad.code, 65);
  assert.throws(() => ensureSuccess("thing", bad), /exited with code 65/);
});

test("handles output far beyond execFile's 1 MB maxBuffer", async () => {
  // xcodebuild test output routinely exceeds 1 MB; the old execFile-based runner died on it.
  const lines: string[] = [];
  const result = await runner.run(node, ["-e", "for (let i=0;i<60000;i++) console.log('line '+i+' '+'x'.repeat(60))"], {
    onLine: (line) => lines.push(line),
    maxCaptureBytes: 64 * 1024
  });
  assert.equal(result.code, 0);
  assert.equal(lines.length, 60000);
  assert.equal(lines[59999].startsWith("line 59999"), true);
  assert.equal(result.truncated, true);
  assert.ok(result.stdout.length <= 64 * 1024 + 200, "capture stays bounded");
  assert.match(result.stdout, /line 59999/, "the tail is what is kept");
});

test("writes the complete output to logFile even when capture is truncated", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "runner-"));
  const logFile = path.join(dir, "out.log");
  await runner.run(node, ["-e", "for (let i=0;i<2000;i++) console.log('row '+i)"], { logFile, maxCaptureBytes: 1024 });
  const text = fs.readFileSync(logFile, "utf-8");
  assert.match(text, /^row 0\n/);
  assert.match(text, /row 1999\n$/);
  fs.rmSync(dir, { recursive: true, force: true });
});

test("delivers a trailing line that has no newline", async () => {
  const lines: string[] = [];
  await runner.run(node, ["-e", "process.stdout.write('a\\nb')"], { onLine: (l) => lines.push(l) });
  assert.deepEqual(lines, ["a", "b"]);
});

test("kills a command that exceeds its timeout", async () => {
  const started = Date.now();
  const result = await runner.run(node, ["-e", "setInterval(()=>{},1000)"], { timeoutMs: 150 });
  assert.equal(result.timedOut, true);
  assert.notEqual(result.code, 0);
  assert.ok(Date.now() - started < 4000);
  assert.throws(() => ensureSuccess("slow", result), /timed out/);
});

test("aborts a running command when the signal fires", async () => {
  const controller = new AbortController();
  const pending = runner.run(node, ["-e", "setInterval(()=>{},1000)"], { signal: controller.signal });
  setTimeout(() => controller.abort(), 150);
  const result = await pending;
  assert.equal(result.aborted, true);
});

test("an already-aborted signal stops the command immediately", async () => {
  const controller = new AbortController();
  controller.abort();
  const result = await runner.run(node, ["-e", "setInterval(()=>{},1000)"], { signal: controller.signal });
  assert.equal(result.aborted, true);
});

test("rejects with a helpful error when the command does not exist", async () => {
  await assert.rejects(runner.run("definitely-not-a-real-binary-xyz", []), (error: Error) => {
    assert.ok(error instanceof CommandError);
    assert.match(error.message, /Command not found/);
    return true;
  });
});

test("runs in the requested working directory", async () => {
  const dir = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "cwd-")));
  const result = await runner.run(node, ["-e", "console.log(process.cwd())"], { cwd: dir });
  assert.equal(result.stdout.trim(), dir);
  fs.rmSync(dir, { recursive: true, force: true });
});
