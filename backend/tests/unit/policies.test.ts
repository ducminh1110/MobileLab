import test from "node:test";
import assert from "node:assert/strict";
import { canTransitionDevice, transitionDeviceState } from "../../src/orchestrator/state/deviceStateMachine";
import { canTransitionJob, transitionJobState } from "../../src/orchestrator/state/jobStateMachine";
import { computeMaxLoad, getActiveLoad } from "../../src/scheduler/policies/capacityPolicy";
import { backoffMs, deviceMatchesJob, isTestCapable } from "../../src/scheduler/services/schedulerService";
import { DomainError } from "../../src/utils/errors";

test("job state machine allows the real lifecycle and nothing else", () => {
  assert.equal(transitionJobState("queued", "running"), "running");
  assert.equal(transitionJobState("running", "retrying"), "retrying");
  assert.equal(transitionJobState("retrying", "queued"), "queued");
  assert.equal(canTransitionJob("queued", "cancelled"), true);
  for (const terminal of ["completed", "failed", "cancelled"] as const) {
    assert.equal(canTransitionJob(terminal, "queued"), false, `${terminal} must be final`);
    assert.equal(canTransitionJob(terminal, "running"), false);
  }
  assert.throws(() => transitionJobState("completed", "running"), (e) => e instanceof DomainError && e.statusCode === 409);
  assert.equal(canTransitionJob("queued", "completed"), false, "a job cannot complete without running");
});

test("device state machine forbids interrupting a busy device", () => {
  assert.equal(transitionDeviceState("stopped", "booting"), "booting");
  assert.equal(canTransitionDevice("busy", "shutting_down"), false);
  assert.equal(canTransitionDevice("booting", "busy"), false);
  assert.equal(canTransitionDevice("error", "booting"), true);
  assert.throws(() => transitionDeviceState("busy", "booting"), /Invalid device transition/);
});

test("capacity counts every booted device, weighted by cost", () => {
  const devices = [
    { type: "simulator", status: "busy" },
    { type: "simulator", status: "ready" },
    { type: "simulator", status: "stopped" },
    { type: "vm", status: "booting" }
  ] as const;
  assert.equal(getActiveLoad([...devices]), 1 + 1 + 4);
  assert.equal(computeMaxLoad(7), 7, "override wins");
  assert.ok(computeMaxLoad() >= 1);
});

test("job/device matching treats unspecified requirements as 'any'", () => {
  const device = { runtime: "rt-18", modelId: "iphone-15" };
  assert.equal(deviceMatchesJob({}, device), true);
  assert.equal(deviceMatchesJob({ requiredRuntime: "rt-18" }, device), true);
  assert.equal(deviceMatchesJob({ requiredRuntime: "rt-17" }, device), false);
  assert.equal(deviceMatchesJob({ requiredRuntime: "rt-18", requiredModelId: "ipad" }, device), false);
});

test("only real simulators are test-capable (the simulated VM must never run tests)", () => {
  assert.equal(isTestCapable({ type: "simulator", canRunTests: true }), true);
  assert.equal(isTestCapable({ type: "vm", canRunTests: false }), false);
  assert.equal(isTestCapable({ type: "vm", canRunTests: true }), false);
});

test("retry backoff grows exponentially and is capped", () => {
  assert.deepEqual([1, 2, 3, 4].map((n) => backoffMs(n)), [500, 1000, 2000, 4000]);
  assert.equal(backoffMs(20), 30_000);
  assert.equal(backoffMs(2, 1), 2);
});
