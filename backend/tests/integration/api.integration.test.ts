import test from "node:test";
import assert from "node:assert/strict";
import { AddressInfo } from "node:net";
import WebSocket from "ws";
import { cleanup, makeApp, waitFor } from "../helpers";

async function withApp<T>(fn: (ctx: Awaited<ReturnType<typeof makeApp>>) => Promise<T>, overrides = {}, env = {}): Promise<T> {
  const ctx = await makeApp(overrides, { ephemeral: true }, env);
  try {
    return await fn(ctx);
  } finally {
    await ctx.close();
    cleanup(ctx.dataDir);
  }
}

test("bad input is a 400 with a readable message, never a 500", async () => {
  await withApp(async ({ app }) => {
    const missing = await app.inject({ method: "POST", url: "/tests/run", payload: {} });
    assert.equal(missing.statusCode, 400);
    assert.equal(missing.json().error, "validation_error");
    assert.match(missing.json().message, /testTarget \(the Xcode scheme\) is required/);

    const badJson = await app.inject({ method: "POST", url: "/devices/spawn", headers: { "content-type": "application/json" }, payload: "{oops" });
    assert.equal(badJson.statusCode, 400, "malformed JSON used to be a 500");

    const flag = await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "-quiet" } });
    assert.equal(flag.statusCode, 400, "a scheme that looks like an xcodebuild flag is rejected");

    const both = await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "A", projectPath: "a.xcodeproj", workspacePath: "a.xcworkspace" } });
    assert.equal(both.statusCode, 400);
    assert.match(both.json().message, /either projectPath or workspacePath/);

    const retries = await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "A", maxRetries: 99 } });
    assert.equal(retries.statusCode, 400);
  });
});

test("unknown things are JSON 404s", async () => {
  await withApp(async ({ app }) => {
    for (const url of ["/devices/nope", "/tests/nope", "/runs/nope", "/tests/nope/results", "/artifacts/nope/download", "/nothing-here"]) {
      const res = await app.inject({ method: "GET", url });
      assert.equal(res.statusCode, 404, url);
      assert.equal(res.json().error, "not_found", url);
    }
    assert.equal((await app.inject({ method: "POST", url: "/devices/boot", payload: { id: "nope" } })).statusCode, 404);
    assert.equal((await app.inject({ method: "GET", url: "/vms/nope/screenshot" })).statusCode, 404, "an unknown VM used to return 200");
  });
});

test("boot/shutdown ignore any client-supplied target state", async () => {
  await withApp(async ({ app }) => {
    const device = (await app.inject({ method: "POST", url: "/devices/spawn", payload: {} })).json();
    const res = await app.inject({ method: "POST", url: "/devices/shutdown", payload: { id: device.id, target: "ready" } });
    assert.equal(res.json().status, "stopped", "the URL is the intent; `target` used to allow arbitrary transitions");
  });
});

test("spawn honours type: vm (the old schema silently dropped it)", async () => {
  await withApp(async ({ app }) => {
    const res = await app.inject({ method: "POST", url: "/devices/spawn", payload: { name: "VM", type: "vm", cpu: 2 } });
    assert.equal(res.statusCode, 200);
    const vm = res.json();
    assert.equal(vm.type, "vm");
    assert.equal(vm.backend, "simulated");
    assert.equal(vm.canRunTests, false);
    assert.equal(vm.cpu, 2);
  });
});

test("the VM API is off outside demo mode unless explicitly enabled", async () => {
  await withApp(async ({ app }) => {
    assert.equal((await app.inject({ method: "GET", url: "/vms" })).statusCode, 501);
    const spawn = await app.inject({ method: "POST", url: "/devices/spawn", payload: { name: "VM", type: "vm" } });
    assert.equal(spawn.statusCode, 501);
    assert.match(spawn.json().message, /IOSLAB_EXPERIMENTAL_VM/);
  }, { experimentalVm: false });
});

test("VM operations work and are labelled simulated", async () => {
  await withApp(async ({ app }) => {
    const vm = (await app.inject({ method: "POST", url: "/vms/spawn", payload: { name: "Sim VM" } })).json();
    assert.equal((await app.inject({ method: "POST", url: `/vms/${vm.id}/backup`, payload: { name: "Snap" } })).json().backupList.includes("Snap"), true);
    assert.equal((await app.inject({ method: "POST", url: `/vms/${vm.id}/restore`, payload: { name: "Nope" } })).statusCode, 404);
    assert.equal((await app.inject({ method: "POST", url: `/vms/${vm.id}/chaos`, payload: { networkProfile: "banana" } })).statusCode, 400);
    assert.equal((await app.inject({ method: "POST", url: `/vms/${vm.id}/chaos`, payload: { networkProfile: "3G" } })).json().networkProfile, "3G");
    const list = (await app.inject({ method: "GET", url: "/vms" })).json();
    assert.equal(list.simulated, true);
    const shot = (await app.inject({ method: "GET", url: `/vms/${vm.id}/screenshot` })).json();
    assert.ok(Buffer.from(shot.image, "base64").subarray(0, 4).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47])));
  });
});

test("screenshots are real PNG data", async () => {
  await withApp(async ({ app }) => {
    const device = (await app.inject({ method: "POST", url: "/devices/spawn", payload: {} })).json();
    const shot = await app.inject({ method: "GET", url: `/devices/${device.id}/screenshot` });
    assert.equal(shot.statusCode, 200);
    assert.equal(shot.headers["content-type"], "image/png");
    assert.ok(shot.rawPayload.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])));
    assert.ok(shot.rawPayload.length > 200);
  });
});

test("token auth protects the API but not the dashboard shell or /health", async () => {
  await withApp(async ({ app }) => {
    assert.equal((await app.inject({ method: "GET", url: "/health" })).statusCode, 200);
    assert.equal((await app.inject({ method: "GET", url: "/" })).statusCode, 200, "the page must load so it can ask for the token");

    const denied = await app.inject({ method: "GET", url: "/devices" });
    assert.equal(denied.statusCode, 401);
    assert.match(denied.headers["www-authenticate"] as string, /Bearer/);
    assert.equal((await app.inject({ method: "GET", url: "/devices", headers: { authorization: "Bearer wrong" } })).statusCode, 401);
    assert.equal((await app.inject({ method: "GET", url: "/devices", headers: { authorization: "Bearer s3cret" } })).statusCode, 200);
    assert.equal((await app.inject({ method: "GET", url: "/devices?token=s3cret" })).statusCode, 200, "query token, for EventSource/WebSocket/downloads");
    assert.equal((await app.inject({ method: "POST", url: "/tests/run", payload: { testTarget: "A" } })).statusCode, 401);
    assert.equal((await app.inject({ method: "POST", url: "/mcp", payload: { jsonrpc: "2.0", id: 1, method: "ping" } })).statusCode, 401);
  }, { apiToken: "s3cret" });
});

test("the dashboard is served with a strict content security policy", async () => {
  await withApp(async ({ app }) => {
    const res = await app.inject({ method: "GET", url: "/" });
    assert.equal(res.statusCode, 200);
    assert.match(res.headers["content-type"] as string, /text\/html/);
    assert.match(res.headers["content-security-policy"] as string, /script-src 'self'/);
    assert.equal(res.headers["x-content-type-options"], "nosniff");
  });
});

test("capabilities, catalog and doctor report real host information", async () => {
  await withApp(async ({ app }) => {
    const caps = (await app.inject({ method: "GET", url: "/capabilities" })).json();
    assert.equal(caps.mode, "demo");
    assert.equal(caps.vm.simulated, true);
    assert.equal(caps.capacity.maxLoad, 4);

    const catalog = (await app.inject({ method: "GET", url: "/catalog" })).json();
    assert.equal(catalog.source, "demo");
    assert.ok(catalog.runtimes.length >= 2 && catalog.deviceTypes.length >= 2);

    const doctor = (await app.inject({ method: "GET", url: "/doctor" })).json();
    assert.equal(doctor.mode, "demo");
    assert.equal(doctor.status, "degraded", "demo mode is never reported as healthy");
    assert.ok(doctor.checks.some((c: { id: string; status: string }) => c.id === "xcodebuild" && c.status === "skip"), "Xcode checks are skipped, not faked as passing");
  });
});

test("metrics reflect live state", async () => {
  await withApp(async ({ app, services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
    await waitFor(() => services.orchestrator.getJob(job.id)?.status === "completed", 5000);
    const text = (await app.inject({ method: "GET", url: "/metrics" })).body;
    assert.match(text, /ioslab_jobs_finished_total\{status="completed"\} 1/);
    assert.match(text, /ioslab_executed_jobs_total 1/);
    assert.match(text, /ioslab_capacity_max 4/);
    assert.match(text, /ioslab_running_jobs 0/);

    const summary = (await app.inject({ method: "GET", url: "/metrics/summary" })).json();
    assert.equal(summary.jobs, 1);
    assert.equal(summary.jobsByStatus.completed, 1);
    assert.equal(summary.queueDepth, 0);
  });
});

test("cleanup endpoint reports what it removed", async () => {
  await withApp(async ({ app, services }) => {
    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
    await waitFor(() => services.orchestrator.getJob(job.id)?.status === "completed", 5000);
    const res = await app.inject({ method: "POST", url: "/maintenance/cleanup", payload: { days: 0 } });
    assert.equal(res.json().jobsRemoved, 1);
    assert.equal((await app.inject({ method: "GET", url: "/tests" })).json().items.length, 0);
  });
});

test("event history is filterable by job", async () => {
  await withApp(async ({ app, services }) => {
    const a = await services.orchestrator.enqueueTest({ testTarget: "AppA" });
    const b = await services.orchestrator.enqueueTest({ testTarget: "AppB" });
    await waitFor(() => [a, b].every((j) => services.orchestrator.getJob(j.id)?.status === "completed"), 5000);
    const events = (await app.inject({ method: "GET", url: `/events?jobId=${a.id}` })).json().items as Array<{ jobId: string; action: string }>;
    assert.ok(events.length > 0);
    assert.ok(events.every((e) => e.jobId === a.id));
    assert.ok(events.some((e) => e.action === "job_finished"));
    assert.ok(events.every((e) => e.action !== "output"), "raw build output is not part of the history");
    const legacy = (await app.inject({ method: "GET", url: `/tests/${a.id}/logs` })).json().items;
    assert.equal(legacy.length, events.length);
  });
});

test("WebSocket /ws/logs streams live events, filtered by job (it never worked before)", async () => {
  await withApp(async ({ app, services }) => {
    await app.listen({ port: 0, host: "127.0.0.1" });
    const port = (app.server.address() as AddressInfo).port;

    const job = await services.orchestrator.enqueueTest({ testTarget: "SlowApp", autoProvision: false });
    const received: Array<{ action: string; jobId?: string; message: string }> = [];
    const socket = new WebSocket(`ws://127.0.0.1:${port}/ws/logs?jobId=${job.id}&output=1`);
    socket.on("message", (data) => received.push(JSON.parse(data.toString())));
    await new Promise<void>((resolve, reject) => {
      socket.once("open", () => resolve());
      socket.once("error", reject);
    });

    await services.orchestrator.spawnDevice({ name: "WS device" });
    await waitFor(() => received.some((e) => e.action === "job_finished"), 8000, "job_finished over WebSocket");

    assert.ok(received.every((e) => e.jobId === job.id), "only this job's events arrive");
    assert.ok(received.some((e) => e.action === "output" && /Test Case/.test(e.message)), "with output=1 build output is streamed live");
    socket.close();
  });
});

test("a closed WebSocket cannot break the engine", async () => {
  await withApp(async ({ app, services }) => {
    await app.listen({ port: 0, host: "127.0.0.1" });
    const port = (app.server.address() as AddressInfo).port;
    const socket = new WebSocket(`ws://127.0.0.1:${port}/ws/events`);
    await new Promise<void>((resolve) => socket.once("open", () => resolve()));
    socket.terminate();
    const job = await services.orchestrator.enqueueTest({ testTarget: "DemoApp" });
    await waitFor(() => services.orchestrator.getJob(job.id)?.status === "completed", 5000);
  });
});

test("Server-Sent Events stream and resume from Last-Event-ID", async () => {
  await withApp(async ({ app, services }) => {
    await app.listen({ port: 0, host: "127.0.0.1" });
    const port = (app.server.address() as AddressInfo).port;
    const first = await services.orchestrator.enqueueTest({ testTarget: "AppA" });
    await waitFor(() => services.orchestrator.getJob(first.id)?.status === "completed", 5000);
    const lastSeen = services.hub.lastId;

    const controller = new AbortController();
    const response = await fetch(`http://127.0.0.1:${port}/events/stream`, { headers: { "last-event-id": String(lastSeen) }, signal: controller.signal });
    assert.equal(response.headers.get("content-type"), "text/event-stream; charset=utf-8");

    const second = await services.orchestrator.enqueueTest({ testTarget: "AppB" });
    const reader = response.body!.getReader();
    let text = "";
    const deadline = Date.now() + 5000;
    while (!text.includes(`"action":"job_finished"`) && Date.now() < deadline) {
      const { value } = await reader.read();
      text += new TextDecoder().decode(value);
    }
    controller.abort();
    assert.match(text, /event: engine/);
    assert.ok(text.includes(second.id), "events after the resume point are delivered");
    assert.ok(!text.includes(first.id), "events before it are not replayed");
  });
});
