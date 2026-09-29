import test from "node:test";
import assert from "node:assert/strict";
import { loadConfig } from "../../src/config/env";

test("defaults are safe: loopback host, no token", () => {
  const config = loadConfig({ NODE_ENV: "test", IOSLAB_SIMULATOR_MOCK: "false" });
  assert.equal(config.host, "127.0.0.1");
  assert.equal(config.port, 4000);
  assert.equal(config.apiToken, undefined);
  assert.equal(config.mock, false);
  assert.equal(config.mockReason, "env");
});

test("mock flag accepts the usual spellings", () => {
  for (const value of ["1", "true", "TRUE", "yes", "on"]) assert.equal(loadConfig({ IOSLAB_SIMULATOR_MOCK: value }).mock, true, value);
  for (const value of ["0", "false", "no", "off"]) assert.equal(loadConfig({ IOSLAB_SIMULATOR_MOCK: value }).mock, false, value);
});

test("an invalid value names the variable instead of silently defaulting", () => {
  assert.throws(() => loadConfig({ PORT: "abc" }), /PORT/);
  assert.throws(() => loadConfig({ IOSLAB_MAX_LOAD: "0" }), /IOSLAB_MAX_LOAD/);
  assert.throws(() => loadConfig({ IOSLAB_SIMULATOR_MOCK: "maybe" }), /IOSLAB_SIMULATOR_MOCK|boolean/);
});

test("VM backend follows demo mode unless set explicitly", () => {
  assert.equal(loadConfig({ IOSLAB_SIMULATOR_MOCK: "true" }).experimentalVm, true);
  assert.equal(loadConfig({ IOSLAB_SIMULATOR_MOCK: "false" }).experimentalVm, false);
  assert.equal(loadConfig({ IOSLAB_SIMULATOR_MOCK: "true", IOSLAB_EXPERIMENTAL_VM: "0" }).experimentalVm, false);
});

test("overrides win over the environment", () => {
  assert.equal(loadConfig({ PORT: "5000" }, { port: 6000 }).port, 6000);
});
