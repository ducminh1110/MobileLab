import test from "node:test";
import assert from "node:assert/strict";
import { cleanup, makeApp } from "../helpers";

async function withApp<T>(fn: (ctx: Awaited<ReturnType<typeof makeApp>>) => Promise<T>, overrides = {}): Promise<T> {
  const ctx = await makeApp(overrides);
  try {
    return await fn(ctx);
  } finally {
    await ctx.close();
    cleanup(ctx.dataDir);
  }
}

const rpc = (app: Awaited<ReturnType<typeof makeApp>>["app"], method: string, params?: unknown, id: number | undefined = 1) =>
  app.inject({ method: "POST", url: "/mcp", payload: { jsonrpc: "2.0", ...(id === undefined ? {} : { id }), method, params } });

test("initialize and tools/list expose JSON-schema inputs", async () => {
  await withApp(async ({ app }) => {
    const init = (await rpc(app, "initialize")).json();
    assert.equal(init.result.serverInfo.name, "mobilelab-mcp-server");

    const tools = (await rpc(app, "tools/list")).json().result.tools as Array<{ name: string; inputSchema: { type: string; properties: Record<string, unknown> } }>;
    const names = tools.map((t) => t.name);
    for (const expected of ["list_devices", "spawn_device", "run_test", "get_job", "cancel_job", "get_screenshot"]) assert.ok(names.includes(expected), expected);
    const runTest = tools.find((t) => t.name === "run_test")!;
    assert.equal(runTest.inputSchema.type, "object");
    assert.ok("scheme" in runTest.inputSchema.properties);
    assert.ok(!("$schema" in runTest.inputSchema));
  });
});

test("VM tools are only offered when the VM backend is enabled", async () => {
  await withApp(async ({ app }) => {
    const names = ((await rpc(app, "tools/list")).json().result.tools as Array<{ name: string }>).map((t) => t.name);
    assert.ok(!names.includes("inject_input"));
    assert.ok(!names.includes("inject_chaos"));
    const call = (await rpc(app, "tools/call", { name: "inject_input", arguments: { id: "x", type: "tap" } })).json();
    assert.equal(call.error.code, -32602);
  }, { experimentalVm: false });

  await withApp(async ({ app }) => {
    const names = ((await rpc(app, "tools/list")).json().result.tools as Array<{ name: string }>).map((t) => t.name);
    assert.ok(names.includes("inject_input"));
  });
});

test("run_test waits for the result by default and returns parsed test results", async () => {
  await withApp(async ({ app }) => {
    const res = (await rpc(app, "tools/call", { name: "run_test", arguments: { scheme: "DemoApp", runtime: "18.0" } })).json();
    assert.equal(res.result.isError, undefined);
    const body = JSON.parse(res.result.content[0].text);
    assert.equal(body.job.status, "completed");
    assert.equal(body.results.summary.total, 8);
  });
});

test("run_test with wait:false returns a queued/running job that get_job can follow", async () => {
  await withApp(async ({ app }) => {
    const started = JSON.parse((await rpc(app, "tools/call", { name: "run_test", arguments: { scheme: "SlowApp", wait: false } })).json().result.content[0].text);
    assert.ok(["queued", "running"].includes(started.job.status));
    const cancelled = (await rpc(app, "tools/call", { name: "cancel_job", arguments: { id: started.job.id } })).json();
    assert.equal(JSON.parse(cancelled.result.content[0].text).status, "cancelled");
  });
});

test("a failing test run is a normal result; a failing tool is isError; bad arguments are isError", async () => {
  await withApp(async ({ app }) => {
    const failing = (await rpc(app, "tools/call", { name: "run_test", arguments: { scheme: "FailingApp" } })).json();
    assert.equal(failing.result.isError, undefined);
    assert.equal(JSON.parse(failing.result.content[0].text).job.status, "failed");

    const badArgs = (await rpc(app, "tools/call", { name: "run_test", arguments: { maxRetries: "many" } })).json();
    assert.equal(badArgs.result.isError, true);
    assert.match(badArgs.result.content[0].text, /Invalid arguments/);

    const noDevice = (await rpc(app, "tools/call", { name: "boot_device", arguments: { id: "nope" } })).json();
    assert.equal(noDevice.result.isError, true);
    assert.match(noDevice.result.content[0].text, /Device not found/);
  });
});

test("get_screenshot returns an image content block", async () => {
  await withApp(async ({ app }) => {
    const device = JSON.parse((await rpc(app, "tools/call", { name: "spawn_device", arguments: { runtime: "18.0", model: "iPhone 15" } })).json().result.content[0].text);
    const shot = (await rpc(app, "tools/call", { name: "get_screenshot", arguments: { id: device.id } })).json().result.content;
    assert.equal(shot[0].type, "image");
    assert.equal(shot[0].mimeType, "image/png");
    assert.ok(Buffer.from(shot[0].data, "base64").subarray(1, 4).toString() === "PNG");
  });
});

test("protocol details: notifications get no reply, unknown methods error, batches work, junk is rejected", async () => {
  await withApp(async ({ app }) => {
    const note = await rpc(app, "notifications/initialized", undefined, undefined);
    assert.equal(note.statusCode, 202);
    assert.equal(note.body, "");

    assert.equal((await rpc(app, "nope")).json().error.code, -32601);
    assert.deepEqual((await rpc(app, "ping")).json().result, {});
    assert.equal((await rpc(app, "tools/call", { name: "nope" })).json().error.code, -32602);

    const batch = await app.inject({ method: "POST", url: "/mcp", payload: [{ jsonrpc: "2.0", id: 1, method: "ping" }, { jsonrpc: "2.0", id: 2, method: "ping" }] });
    assert.equal(batch.json().length, 2);

    assert.equal((await app.inject({ method: "POST", url: "/mcp", payload: { not: "rpc" } })).statusCode, 400);
    assert.equal((await app.inject({ method: "POST", url: "/mcp", payload: [] })).statusCode, 400);
  });
});
