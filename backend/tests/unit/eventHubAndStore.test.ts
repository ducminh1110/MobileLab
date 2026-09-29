import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { EventHub } from "../../src/core/eventHub";
import { emptyState, StateStore } from "../../src/store/stateStore";

const event = (message: string, extra = {}) => ({ source: "system" as const, type: "log" as const, action: "t", message, ...extra });

test("a throwing subscriber cannot break the emitter (the old bus let a closed socket crash the engine)", () => {
  const hub = new EventHub();
  const seen: string[] = [];
  hub.subscribe(() => {
    throw new Error("socket closed");
  });
  hub.subscribe((e) => seen.push(e.message));
  hub.emit(event("one"));
  hub.emit(event("two"));
  assert.deepEqual(seen, ["one", "two"]);
});

test("ids are monotonic; history is bounded; filters work", () => {
  const hub = new EventHub(3);
  for (let i = 0; i < 5; i += 1) hub.emit(event(`m${i}`, { jobId: i % 2 ? "b" : "a" }));
  assert.deepEqual(hub.list().map((e) => e.message), ["m2", "m3", "m4"]);
  assert.deepEqual(hub.list({ jobId: "a" }).map((e) => e.message), ["m2", "m4"]);
  assert.deepEqual(hub.list({ sinceId: 4 }).map((e) => e.id), [5]);
  assert.equal(hub.lastId, 5);
});

test("transient events reach subscribers but never the history", () => {
  const hub = new EventHub();
  const seen: string[] = [];
  hub.subscribe((e) => seen.push(e.message));
  hub.emitTransient(event("noise"));
  hub.emit(event("signal"));
  assert.deepEqual(seen, ["noise", "signal"]);
  assert.deepEqual(hub.list().map((e) => e.message), ["signal"]);
});

test("state store round-trips and writes atomically", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "store-"));
  const file = path.join(dir, "state.json");
  const store = new StateStore(file, 5);
  const state = { ...emptyState(), runs: [{ id: "r1", scheme: "S", jobIds: [], createdAt: "now" }] };
  store.scheduleSave(() => state);
  store.flush();
  assert.deepEqual(fs.readdirSync(dir), ["state.json"], "no temp file left behind");
  assert.equal(new StateStore(file).load().runs[0].id, "r1");
  fs.rmSync(dir, { recursive: true, force: true });
});

test("a corrupt state file is set aside, not fatal", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "store-"));
  const file = path.join(dir, "state.json");
  fs.writeFileSync(file, "{ this is not json");
  const store = new StateStore(file);
  assert.deepEqual(store.load(), emptyState());
  assert.ok(store.recoveredFrom && fs.existsSync(store.recoveredFrom), "the bad file is kept for inspection");
  fs.rmSync(dir, { recursive: true, force: true });
});
