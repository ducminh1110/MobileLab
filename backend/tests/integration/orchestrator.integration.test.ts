import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { cleanup, makeApp, waitFor } from "../helpers";
import { TestJob } from "../../src/simulator/models/types";
import { CommandResult, CommandRunner, RunOptions } from "../../src/simulator/engine/commandRunner";
import { MockCommandRunner } from "../../src/simulator/engine/mockCommandRunner";

const RT_18 = "com.apple.CoreSimulator.SimRuntime.iOS-18-0";
const RT_17 = "com.apple.CoreSimulator.SimRuntime.iOS-17-5";

async function withApp<T>(fn: (ctx: Awaited<ReturnType<typeof makeApp>>) => Promise<T>, overrides = {}, env = {}): Promise<T> {
  const ctx = await makeApp(overrides, { ephemeral: true }, env);
  try {
    return await fn(ctx);
  } finally {
    await ctx.close();
    cleanup(ctx.dataDir);
  }
}

const finished = (o: { getJob(id: string): TestJob | undefined }, id: string) =>
  waitFor(() => {
    const job = o.getJob(id);
    return job && ["completed", "failed", "cancelled"].includes(job.status) ? job : undefined;
  }, 8000, `job ${id} to finish`);

test("spawn a simulator, run tests on it, and read results, log, artifacts and JUnit", async () => {
  await withApp(async ({ app, services }) => {
    const spawn = await app.inject({ method: "POST", url: "/devices/spawn", payload: { name: "Test iPhone", runtime: "18.0", modelId: "iPhone 15" } });
    assert.equal(spawn.statusCode, 200);
    const device = spawn.json();
    assert.equal(device.status, "ready");
    assert.equal(device.runtime, RT_18, "shorthand resolved to the real identifier");
    assert.equal(device.backend, "simctl");

    const run = await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "DemoApp", wait: true } });
    assert.equal(run.statusCode, 200);
    const job = run.json().job as TestJob;
    assert.equal(job.status, "completed");
    assert.equal(job.assignedDeviceId, device.id, "the existing device is reused");
    assert.equal(job.summary?.total, 8);
    assert.equal(job.summary?.failed, 0);

    const results = (await app.inject({ method: "GET", url: `/tests/${job.id}/results` })).json();
    assert.equal(results.cases.length, 8);

    const artifacts = (await app.inject({ method: "GET", url: `/tests/${job.id}/artifacts` })).json().items as Array<{ type: string; downloadUrl?: string; id: string; isDirectory: boolean }>;
    assert.deepEqual(artifacts.map((a) => a.type).sort(), ["log", "results", "xcresult"]);
    const xcresult = artifacts.find((a) => a.type === "xcresult")!;
    assert.equal(xcresult.isDirectory, true, "the result bundle stays a directory (the old code overwrote it with a text file)");

    const log = artifacts.find((a) => a.type === "log")!;
    const download = await app.inject({ method: "GET", url: log.downloadUrl! });
    assert.equal(download.statusCode, 200);
    assert.match(download.headers["content-type"] as string, /text\/plain/);
    assert.match(download.body, /\*\* TEST SUCCEEDED \*\*/);

    const dir = await app.inject({ method: "GET", url: `/artifacts/${xcresult.id}/download` });
    assert.equal(dir.statusCode, 400);

    const junit = await app.inject({ method: "GET", url: `/tests/${job.id}/junit` });
    assert.equal(junit.statusCode, 200);
    assert.match(junit.body, /<testsuites tests="8" failures="0"/);

    const output = (await app.inject({ method: "GET", url: `/tests/${job.id}/output` })).json();
    assert.match(output.text, /TEST SUCCEEDED/);
    assert.equal((await app.inject({ method: "GET", url: `/devices/${device.id}` })).json().status, "ready", "device is released");
    assert.ok(services.hub.list({ jobId: job.id }).length >= 2, "job lifecycle events are tagged with the job id");
  });
});

test("failing tests fail the job with a readable reason and are not retried by default", async () => {
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "FailingApp" });
    const done = await finished(services.orchestrator, job.id);
    assert.equal(done.status, "failed");
    assert.equal(done.error, "2 of 8 tests failed");
    assert.equal(done.summary?.failed, 2);
    assert.equal(done.attempts, 1);
    assert.equal(done.exitCode, 65);
    const failed = services.orchestrator.getJobResults(job.id)!.cases.filter((c) => c.status === "failed");
    assert.match(failed[0].message!, /XCTAssertEqual failed/);
    assert.equal(services.orchestrator.getJobJUnit(job.id).includes("<failure"), true);
  });
});

test("a flaky job really is retried and can pass on the second attempt", async () => {
  // The old scheduler re-queued a retry but nothing ever ran it again.
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "FlakyApp", maxRetries: 2 });
    const done = await finished(services.orchestrator, job.id);
    assert.equal(done.status, "completed");
    assert.equal(done.attempts, 2);
    assert.equal(done.retries, 1);
    assert.equal(services.orchestrator.getJobResults(job.id)!.attempt, 2);
  });
});

test("retries stop at maxRetries", async () => {
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "FailingApp", maxRetries: 2 });
    const done = await finished(services.orchestrator, job.id);
    assert.equal(done.status, "failed");
    assert.equal(done.attempts, 3);
    assert.equal(done.retries, 2);
  });
});

test("a build error (missing scheme) is not retried", async () => {
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "MissingScheme", maxRetries: 3 });
    const done = await finished(services.orchestrator, job.id);
    assert.equal(done.status, "failed");
    assert.equal(done.attempts, 1, "retrying a deterministic failure only wastes the queue");
    assert.match(done.error!, /does not contain a scheme named "MissingScheme"/);
    assert.equal(done.summary?.buildFailed, true);
  });
});

test("the simulated VM never runs tests: no false 'passed' result", async () => {
  // Regression: a pre-seeded fake VM used to be picked for real jobs and 'passed' after a 200ms sleep.
  await withApp(async ({ services }) => {
    const vm = await services.orchestrator.spawnDevice({ name: "Sim VM", type: "vm" });
    assert.equal(vm.canRunTests, false);
    assert.equal(vm.backend, "simulated");

    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp", autoProvision: false });
    await new Promise((resolve) => setTimeout(resolve, 100));
    const current = services.orchestrator.getJob(job.id)!;
    assert.equal(current.status, "queued");
    assert.match(current.waitingReason!, /No ready simulator matches/);
    assert.equal(services.orchestrator.getDevice(vm.id)!.status, "ready", "the VM was left alone");
  });
});

test("there are no pre-seeded devices", async () => {
  await withApp(async ({ services }) => assert.equal(services.orchestrator.listDevices().length, 0));
});

test("POST /tests/run returns immediately; a running job can be cancelled and frees its device", async () => {
  await withApp(async ({ app, services }) => {
    const started = Date.now();
    const res = await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "SlowApp" } });
    assert.equal(res.statusCode, 202);
    assert.ok(Date.now() - started < 1000, "the request is not held open for the whole test run");

    const id = res.json().job.id as string;
    await waitFor(() => services.orchestrator.getJob(id)?.status === "running", 5000, "job to start");
    const cancel = await app.inject({ method: "POST", url: `/tests/${id}/cancel` });
    assert.equal(cancel.statusCode, 200);
    assert.equal(cancel.json().status, "cancelled");

    const again = await app.inject({ method: "POST", url: `/tests/${id}/cancel` });
    assert.equal(again.statusCode, 409, "cancelling a finished job is a conflict");

    await waitFor(() => services.orchestrator.listDevices().length === 0, 5000, "the auto-created simulator to be removed");
  });
});

test("a queued job starts as soon as a matching device becomes ready", async () => {
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp", autoProvision: false });
    assert.equal(services.orchestrator.getJob(job.id)!.status, "queued");
    await services.orchestrator.spawnDevice({ name: "Late device" });
    assert.equal((await finished(services.orchestrator, job.id)).status, "completed");
  });
});

test("auto-provisioning creates a simulator for the job and removes it afterwards", async () => {
  await withApp(async ({ services }) => {
    const seen: string[] = [];
    services.hub.subscribe((e) => e.action === "spawn_device" && seen.push(e.message));
    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp", requiredRuntime: "17.5", requiredModelId: "iPhone SE (3rd generation)" });
    const done = await finished(services.orchestrator, job.id);
    assert.equal(done.status, "completed");
    assert.equal(done.requiredRuntime, RT_17);
    assert.equal(done.assignedDeviceName, "iPhone SE (3rd generation) (iOS 17.5)");
    await waitFor(() => services.orchestrator.listDevices().length === 0, 5000, "ephemeral device cleanup");
    assert.equal(seen.length, 1);
  });
});

test("a run fans out over runtimes x device types and honours maxParallel", async () => {
  await withApp(async ({ app, services }) => {
    let running = 0;
    let peak = 0;
    services.hub.subscribe((e) => {
      if (e.action === "run_job") running += 1;
      if (e.action === "job_finished") running -= 1;
      peak = Math.max(peak, running);
    });

    const res = await app.inject({ method: "POST", url: "/runs", payload: { scheme: "SlowApp", runtimes: ["18.0", "17.5"], models: ["iPhone 15", "iPad (10th generation)"], maxParallel: 2 } });
    assert.equal(res.statusCode, 202);
    const { run, jobs } = res.json();
    assert.equal(jobs.length, 4);
    assert.equal(run.status === "queued" || run.status === "running", true);

    await waitFor(() => services.orchestrator.getRun(run.id)!.status === "passed", 15000, "run to pass");
    assert.equal(peak, 2, "never more than maxParallel jobs at once, but does use the parallelism");
    const view = (await app.inject({ method: "GET", url: `/runs/${run.id}` })).json();
    assert.equal(view.run.counts.completed, 4);
    assert.equal(view.jobs.length, 4);
    await waitFor(() => services.orchestrator.listDevices().length === 0, 5000, "ephemeral devices to be removed");
  });
});

test("a run with a failing scheme reports failed; cancelling a run cancels what is left", async () => {
  await withApp(async ({ app, services }) => {
    const failing = (await app.inject({ method: "POST", url: "/runs", payload: { scheme: "FailingApp", runtimes: ["18.0"] } })).json().run;
    await waitFor(() => services.orchestrator.getRun(failing.id)!.status === "failed", 8000, "run to fail");

    const slow = (await app.inject({ method: "POST", url: "/runs", payload: { scheme: "SlowApp", runtimes: ["18.0", "17.5"], maxParallel: 1 } })).json().run;
    await waitFor(() => services.orchestrator.getRun(slow.id)!.counts.running === 1, 5000, "first job to run");
    const cancelled = (await app.inject({ method: "POST", url: `/runs/${slow.id}/cancel` })).json().run;
    assert.equal(cancelled.status, "cancelled");
    assert.equal(cancelled.counts.cancelled, 2);
  });
});

test("a matrix skips combinations that cannot exist and says so", async () => {
  await withApp(async ({ app }) => {
    const res = await app.inject({ method: "POST", url: "/runs", payload: { scheme: "X", runtimes: ["18.2", "18.0", "17.5"], models: ["iPhone SE (3rd generation)", "iPhone 15", "iPhone 16 Pro", "iPad (10th generation)"] } });
    assert.equal(res.statusCode, 202);
    const body = res.json();
    assert.equal(body.jobs.length, 11, "12 combinations minus the one that does not exist");
    assert.deepEqual(body.skipped, [{ runtime: "iOS 17.5", model: "iPhone 16 Pro", reason: "iPhone 16 Pro is not available on iOS 17.5" }]);

    const impossible = await app.inject({ method: "POST", url: "/runs", payload: { scheme: "X", runtimes: ["17.5"], models: ["iPhone 16 Pro"] } });
    assert.equal(impossible.statusCode, 400);
    assert.match(impossible.json().message, /None of the requested combinations exist/);

    const tooMany = await app.inject({ method: "POST", url: "/runs", payload: { scheme: "X", runtimes: Array.from({ length: 17 }, (_, i) => `1${i}.0`) } });
    assert.equal(tooMany.statusCode, 400);
  });
});

test("device types are checked against the runtime, and defaults fit it", async () => {
  await withApp(async ({ app, services }) => {
    const bad = await app.inject({ method: "POST", url: "/devices/spawn", payload: { runtime: "17.5", modelId: "iPhone 16 Pro" } });
    assert.equal(bad.statusCode, 400);
    assert.match(bad.json().message, /iPhone 16 Pro is not available on iOS 17\.5\. Available there: /);

    const fitted = await services.orchestrator.spawnDevice({ modelId: "iPhone 16 Pro" });
    assert.equal(fitted.runtimeName, "iOS 18.2", "an unspecified runtime is chosen so the device type exists on it");

    const old = await services.orchestrator.spawnDevice({ runtime: "17.5" });
    assert.equal(old.modelName, "iPhone 15", "the default device type is the newest one that iOS 17.5 supports");

    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp", requiredModelId: "iPhone 16 Pro", autoProvision: false });
    assert.equal(job.requiredRuntime, undefined);
    await assert.rejects(services.orchestrator.enqueueTest({ testTarget: "DemoApp", requiredRuntime: "17.5", requiredModelId: "iPhone 16 Pro" }), /not available on iOS 17\.5/);
  }, {}, { IOSLAB_MAX_LOAD: "8" });
});

test("capacity: spawning past the limit is a 429, and a job waits for capacity instead of failing", async () => {
  await withApp(
    async ({ app, services }) => {
      const first = await app.inject({ method: "POST", url: "/devices/spawn", payload: { runtime: "18.0" } });
      assert.equal(first.statusCode, 200);
      const second = await app.inject({ method: "POST", url: "/devices/spawn", payload: { runtime: "18.0" } });
      assert.equal(second.statusCode, 429);
      assert.match(second.json().message, /Capacity exceeded: 1 of 1/);

      const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp", requiredRuntime: "17.5" });
      await new Promise((resolve) => setTimeout(resolve, 50));
      assert.match(services.orchestrator.getJob(job.id)!.waitingReason!, /Waiting for capacity/);

      // Freeing capacity lets the job create its own simulator and run.
      await app.inject({ method: "DELETE", url: `/devices/${first.json().id}` });
      assert.equal((await finished(services.orchestrator, job.id)).status, "completed");
    },
    {},
    { IOSLAB_MAX_LOAD: "1" }
  );
});

test("device lifecycle: boot and shutdown are idempotent, delete removes it from the host", async () => {
  await withApp(async ({ app, services }) => {
    const device = (await app.inject({ method: "POST", url: "/devices/spawn", payload: {} })).json();
    assert.equal((await app.inject({ method: "POST", url: "/devices/boot", payload: { id: device.id } })).json().status, "ready", "booting a booted device is a no-op");

    const stopped = await app.inject({ method: "POST", url: `/devices/${device.id}/shutdown` });
    assert.equal(stopped.json().status, "stopped");
    assert.equal((await app.inject({ method: "POST", url: `/devices/${device.id}/shutdown` })).json().status, "stopped", "so is shutting down twice");
    assert.equal((await app.inject({ method: "GET", url: `/devices/${device.id}/screenshot` })).statusCode, 409, "a stopped device has no screen");

    assert.equal((await app.inject({ method: "POST", url: `/devices/${device.id}/boot` })).json().status, "ready");
    assert.equal((await app.inject({ method: "DELETE", url: `/devices/${device.id}` })).statusCode, 204);
    assert.equal((await app.inject({ method: "GET", url: `/devices/${device.id}` })).statusCode, 404);
    assert.equal((await services.engine.simctl.list()).length, 0, "the simulator is gone from the host, not just from the pool");
  });
});

test("a busy device cannot be shut down or deleted", async () => {
  await withApp(async ({ app, services }) => {
    const device = (await app.inject({ method: "POST", url: "/devices/spawn", payload: {} })).json();
    const job = await services.orchestrator.enqueueTest({ testTarget: "SlowApp" });
    await waitFor(() => services.orchestrator.getDevice(device.id)?.status === "busy", 5000, "device to be busy");
    assert.equal((await app.inject({ method: "POST", url: `/devices/${device.id}/shutdown` })).statusCode, 409);
    assert.equal((await app.inject({ method: "DELETE", url: `/devices/${device.id}` })).statusCode, 409);
    await finished(services.orchestrator, job.id);
  });
});

test("concurrent spawns get distinct ids and all boot", async () => {
  await withApp(async ({ services }) => {
    const devices = await Promise.all(["A", "B", "C"].map((name) => services.orchestrator.spawnDevice({ name })));
    assert.equal(new Set(devices.map((d) => d.id)).size, 3);
    assert.equal(new Set(devices.map((d) => d.simulatorUdid)).size, 3, "each device owns its own simulator");
    assert.deepEqual(devices.map((d) => d.name), ["A", "B", "C"]);
    assert.ok(devices.every((d) => d.status === "ready"));
  });
});

test("a VM (cost 4) consumes capacity a simulator (cost 1) would not", async () => {
  await withApp(async ({ services }) => {
    await services.orchestrator.spawnDevice({ name: "VM one", type: "vm" });
    await assert.rejects(services.orchestrator.spawnDevice({ name: "VM two", type: "vm" }), /Capacity exceeded: 4 of 4/);
  });
});

test("failing to create a simulator surfaces a clear error and leaves nothing behind", async () => {
  await withApp(async ({ app, services }) => {
    const res = await app.inject({ method: "POST", url: "/devices/spawn", payload: { runtime: "9.9" } });
    assert.equal(res.statusCode, 400);
    assert.match(res.json().message, /Unknown runtime "9\.9"/);
    assert.equal(services.orchestrator.listDevices().length, 0);
  });
});

test("state survives a restart; interrupted jobs are marked failed rather than left running", async () => {
  const first = await makeApp({}, { ephemeral: false });
  const job = await first.services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
  await finished(first.services.orchestrator, job.id);
  await first.close();

  // Forge what a crash mid-run would leave on disk.
  const stateFile = path.join(first.dataDir, "state.json");
  const state = JSON.parse(fs.readFileSync(stateFile, "utf-8"));
  state.jobs.push({ ...state.jobs[0], id: "crashed-job", status: "running", finishedAt: undefined });
  fs.writeFileSync(stateFile, JSON.stringify(state));

  const second = await makeApp({ dataDir: first.dataDir }, { ephemeral: false });
  try {
    const restored = second.services.orchestrator.getJob(job.id)!;
    assert.equal(restored.status, "completed");
    assert.equal(second.services.orchestrator.getJobResults(job.id)!.cases.length, 8, "results are still readable");
    const crashed = second.services.orchestrator.getJob("crashed-job")!;
    assert.equal(crashed.status, "failed");
    assert.match(crashed.error!, /backend restarted/);
  } finally {
    await second.close();
    cleanup(first.dataDir);
  }
});

test("devices that vanished from the host are dropped on startup", async () => {
  const first = await makeApp({}, { ephemeral: false });
  await first.services.orchestrator.spawnDevice({ name: "Keeper" });
  await first.close();

  // A fresh mock host has no simulators, like a Mac whose simulators were wiped.
  const second = await makeApp({ dataDir: first.dataDir }, { ephemeral: false });
  try {
    assert.equal(second.services.orchestrator.listDevices().length, 0);
  } finally {
    await second.close();
    cleanup(first.dataDir);
  }
});

test("cleanup removes finished jobs, their files and empty runs; keeps recent ones", async () => {
  await withApp(async ({ services }) => {
    const old = await services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
    await finished(services.orchestrator, old.id);
    const dir = services.orchestrator.artifacts.dirFor(old.id);
    assert.ok(fs.existsSync(dir));

    assert.equal(services.orchestrator.cleanup(1).jobsRemoved, 0, "recent jobs are kept");
    const result = services.orchestrator.cleanup(0);
    assert.equal(result.jobsRemoved, 1);
    assert.ok(result.bytesFreed > 0);
    assert.equal(fs.existsSync(dir), false);
    assert.equal(services.orchestrator.getJob(old.id), undefined);
  });
});

test("rerun creates a fresh job with the same parameters", async () => {
  await withApp(async ({ services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "FailingApp", onlyTesting: ["AppTests/LoginTests"] });
    await finished(services.orchestrator, job.id);
    const copy = await services.orchestrator.rerunJob(job.id);
    assert.notEqual(copy.id, job.id);
    assert.equal(copy.testTarget, "FailingApp");
    assert.deepEqual(copy.onlyTesting, ["AppTests/LoginTests"]);
    await assert.rejects(services.orchestrator.rerunJob(copy.id), /still/, "cannot rerun a job that has not finished");
    await finished(services.orchestrator, copy.id);
  });
});

test("a job is never given two simulators while its first one is still being resolved", async () => {
  // On a real Mac the first `simctl list runtimes` takes about a second; a pump triggered meanwhile used
  // to create a second simulator for the same queued job.
  const inner = new MockCommandRunner();
  const slowCatalog: CommandRunner = {
    async run(command: string, args: string[], options?: RunOptions): Promise<CommandResult> {
      if (command === "xcrun" && args[0] === "simctl" && args[1] === "list" && args[2] !== "devices") {
        await new Promise((resolve) => setTimeout(resolve, 80));
      }
      return inner.run(command, args, options);
    }
  };
  const ctx = await makeApp({}, { ephemeral: true, runner: slowCatalog });
  try {
    const spawned: string[] = [];
    ctx.services.hub.subscribe((e) => e.action === "spawn_device" && e.type === "started" && spawned.push(e.message));
    const job = await ctx.services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
    // Unrelated activity that pumps the dispatcher while the catalog is still loading.
    await ctx.services.orchestrator.enqueueTest({ testTarget: "Other", autoProvision: false });
    await ctx.services.orchestrator.syncDevices();
    await finished(ctx.services.orchestrator, job.id);
    assert.equal(spawned.length, 1, `expected one simulator, got: ${spawned.join(", ")}`);
  } finally {
    await ctx.close();
    cleanup(ctx.dataDir);
  }
});
