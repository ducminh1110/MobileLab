import test from "node:test";
import assert from "node:assert/strict";
import { MockCommandRunner } from "../../src/simulator/engine/mockCommandRunner";
import { SimulatorEngine } from "../../src/simulator/engine/simulatorEngine";
import { XcodebuildClient } from "../../src/simulator/engine/xcodebuildClient";
import { DomainError } from "../../src/utils/errors";

const engine = () => new SimulatorEngine(new MockCommandRunner(), process.cwd());

test("runtime shorthand resolves to the identifier simctl needs", async () => {
  const { catalog } = engine();
  const id = "com.apple.CoreSimulator.SimRuntime.iOS-17-5";
  for (const input of [id, "iOS 17.5", "17.5", "iOS-17-5", "ios 17.5"]) {
    assert.equal((await catalog.resolveRuntime(input)).identifier, id, input);
  }
  assert.equal((await catalog.resolveRuntime("18")).version, "18.2", "a bare major version picks the newest minor");
  assert.equal((await catalog.resolveRuntime()).version, "18.2", "no input picks the newest runtime");
});

test("an unknown runtime is a 400 that lists what is available", async () => {
  await assert.rejects(engine().catalog.resolveRuntime("12.0"), (error: unknown) => {
    assert.ok(error instanceof DomainError);
    assert.equal(error.statusCode, 400);
    assert.match(error.message, /Available: iOS 18\.2, iOS 18\.0, iOS 17\.5/);
    return true;
  });
});

test("device type shorthand and default", async () => {
  const { catalog } = engine();
  assert.equal((await catalog.resolveDeviceType("iPhone 15")).identifier, "com.apple.CoreSimulator.SimDeviceType.iPhone-15");
  assert.equal((await catalog.resolveDeviceType("iPhone-15")).name, "iPhone 15");
  assert.equal((await catalog.resolveDeviceType()).name, "iPhone 15", "default is the newest plain iPhone");
  await assert.rejects(catalog.resolveDeviceType("Nokia 3310"), /Unknown device type/);
});

test("xcodebuild args: relative project resolves against the workspace root, flags are rejected", () => {
  const client = new XcodebuildClient(new MockCommandRunner(), "/work");
  const { args, cwd } = client.buildTestArgs({
    scheme: "App",
    destination: "platform=iOS Simulator,id=X",
    projectPath: "ios/App.xcodeproj",
    resultBundlePath: "/tmp/r.xcresult",
    onlyTesting: ["AppTests/LoginTests"]
  });
  assert.deepEqual(args.slice(0, 3), ["test", "-project", "/work/ios/App.xcodeproj"]);
  assert.equal(cwd, "/work/ios");
  assert.ok(args.includes("-only-testing:AppTests/LoginTests"));
  assert.ok(!args.includes("-configuration"), "no hard-coded configuration");
  assert.throws(() => client.buildTestArgs({ scheme: "-quiet", destination: "d", resultBundlePath: "r" }), /Invalid scheme/);
});

test("mock simctl mirrors real failure modes", async () => {
  const runner = new MockCommandRunner();
  const run = (...args: string[]) => runner.run("xcrun", ["simctl", ...args]);
  const created = await run("create", "T", "com.apple.CoreSimulator.SimDeviceType.iPhone-15", "com.apple.CoreSimulator.SimRuntime.iOS-18-0");
  const udid = created.stdout.trim();
  assert.equal((await run("boot", udid)).code, 0);
  assert.equal((await run("boot", udid)).code, 149, "booting twice fails like the real tool");
  assert.equal((await run("delete", udid)).code, 149, "cannot delete a booted device");
  assert.equal((await run("create", "T", "nope", "com.apple.CoreSimulator.SimRuntime.iOS-18-0")).code, 1);
  assert.equal((await run("boot", "00000000-0000-0000-0000-000000000000")).code, 164);
});
