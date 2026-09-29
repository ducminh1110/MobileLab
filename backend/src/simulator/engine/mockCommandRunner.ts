import { randomUUID } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { CommandResult, CommandRunner, RunOptions } from "./commandRunner";
import { phonePlaceholderPng } from "./pngPlaceholder";

interface MockSim {
  udid: string;
  name: string;
  runtime: string;
  deviceType: string;
  state: "Shutdown" | "Booted" | "Booting";
}

export interface MockRunnerOptions {
  /** Base delay of simulated operations, so the UI can show intermediate states. */
  latencyMs?: number;
}

const RUNTIMES = [
  { identifier: "com.apple.CoreSimulator.SimRuntime.iOS-17-5", name: "iOS 17.5", version: "17.5", buildversion: "21F79" },
  { identifier: "com.apple.CoreSimulator.SimRuntime.iOS-18-0", name: "iOS 18.0", version: "18.0", buildversion: "22A3351" },
  { identifier: "com.apple.CoreSimulator.SimRuntime.iOS-18-2", name: "iOS 18.2", version: "18.2", buildversion: "22C150" }
];

const DEVICE_TYPES = [
  { identifier: "com.apple.CoreSimulator.SimDeviceType.iPhone-SE-3rd-generation", name: "iPhone SE (3rd generation)", productFamily: "iPhone" },
  { identifier: "com.apple.CoreSimulator.SimDeviceType.iPhone-15", name: "iPhone 15", productFamily: "iPhone" },
  { identifier: "com.apple.CoreSimulator.SimDeviceType.iPhone-16-Pro", name: "iPhone 16 Pro", productFamily: "iPhone" },
  { identifier: "com.apple.CoreSimulator.SimDeviceType.iPad-10th-generation", name: "iPad (10th generation)", productFamily: "iPad" }
];

function hashOf(text: string): number {
  let h = 2166136261;
  for (let i = 0; i < text.length; i += 1) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

function abortableSleep(ms: number, signal?: AbortSignal): Promise<void> {
  if (ms <= 0) return Promise.resolve();
  return new Promise((resolve) => {
    const timer = setTimeout(done, ms);
    function done() {
      clearTimeout(timer);
      signal?.removeEventListener("abort", done);
      resolve();
    }
    signal?.addEventListener("abort", done, { once: true });
  });
}

function result(code: number, stdout = "", stderr = "", extra: Partial<CommandResult> = {}): CommandResult {
  return { stdout, stderr, code, durationMs: 0, timedOut: false, aborted: false, truncated: false, ...extra };
}

const SUITES: Array<{ className: string; tests: string[] }> = [
  { className: "LoginTests", tests: ["testValidCredentials", "testInvalidPassword", "testEmptyUsername"] },
  { className: "CheckoutTests", tests: ["testAddToCart", "testApplyCoupon", "testPaymentDeclined"] },
  { className: "SettingsTests", tests: ["testToggleDarkMode", "testLogout"] }
];

/**
 * A stateful stand-in for `xcrun simctl` and `xcodebuild`, used in demo mode and in tests. It is
 * deliberately strict about the same things the real tools are strict about (unknown runtime, booting
 * a booted device, testing on a device that is not booted) so client code is exercised on its real
 * error paths.
 *
 * Scheme names steer the outcome of `xcodebuild test`: a name containing "fail" produces failing
 * tests, "flaky" fails the first run per device and passes afterwards, "missing" reports an unknown
 * scheme, "slow" runs long enough to cancel.
 */
export class MockCommandRunner implements CommandRunner {
  private readonly sims = new Map<string, MockSim>();
  private readonly flakySeen = new Set<string>();
  private readonly latency: number;

  constructor(options: MockRunnerOptions = {}) {
    this.latency = options.latencyMs ?? 0;
  }

  async run(command: string, args: string[], options: RunOptions = {}): Promise<CommandResult> {
    if (command === "xcrun" && args[0] === "simctl") return this.simctl(args.slice(1), options);
    if (command === "xcodebuild") return this.xcodebuild(args, options);
    if (command === "xcode-select") return result(0, "/Applications/Xcode.app/Contents/Developer\n");
    return result(127, "", `mock runner: unsupported command ${command}\n`);
  }

  private async simctl(args: string[], options: RunOptions): Promise<CommandResult> {
    const [sub, ...rest] = args;

    if (sub === "list") {
      const what = rest[0];
      if (what === "runtimes") {
        return result(
          0,
          JSON.stringify({
            runtimes: RUNTIMES.map((r) => ({ ...r, platform: "iOS", isAvailable: true, isInternal: false }))
          })
        );
      }
      if (what === "devicetypes") {
        return result(0, JSON.stringify({ devicetypes: DEVICE_TYPES }));
      }
      const devices: Record<string, unknown[]> = {};
      for (const sim of this.sims.values()) {
        (devices[sim.runtime] ??= []).push({
          udid: sim.udid,
          name: sim.name,
          state: sim.state,
          isAvailable: true,
          deviceTypeIdentifier: sim.deviceType
        });
      }
      return result(0, JSON.stringify({ devices }));
    }

    if (sub === "create") {
      const [name, deviceType, runtime] = rest;
      if (!DEVICE_TYPES.some((d) => d.identifier === deviceType)) return result(1, "", `Invalid device type: ${deviceType}\n`);
      if (!RUNTIMES.some((r) => r.identifier === runtime)) return result(1, "", `Invalid runtime: ${runtime}\n`);
      await abortableSleep(this.latency, options.signal);
      const udid = randomUUID().toUpperCase();
      this.sims.set(udid, { udid, name, runtime, deviceType, state: "Shutdown" });
      return result(0, `${udid}\n`);
    }

    const udid = rest[0];
    if (sub === "boot" || sub === "bootstatus" || sub === "shutdown" || sub === "erase" || sub === "delete" || sub === "io") {
      const sim = this.sims.get(udid);
      if (!sim) return result(164, "", `Invalid device: ${udid}\n`);

      if (sub === "boot") {
        if (sim.state === "Booted") return result(149, "", "Unable to boot device in current state: Booted\n");
        await abortableSleep(this.latency * 2, options.signal);
        sim.state = "Booted";
        return result(0);
      }
      if (sub === "bootstatus") {
        if (sim.state !== "Booted") return result(1, "", "Unable to get bootstatus: device is not booted\n");
        return result(0, "Status=3, isTerminal=YES\n");
      }
      if (sub === "shutdown") {
        if (sim.state === "Shutdown") return result(149, "", "Unable to shutdown device in current state: Shutdown\n");
        await abortableSleep(this.latency, options.signal);
        sim.state = "Shutdown";
        return result(0);
      }
      if (sub === "erase") {
        if (sim.state !== "Shutdown") return result(149, "", "Unable to erase contents and settings in current state: Booted\n");
        return result(0);
      }
      if (sub === "delete") {
        if (sim.state !== "Shutdown") return result(149, "", "Unable to delete device in current state: Booted\n");
        await abortableSleep(Math.round(this.latency / 2), options.signal);
        this.sims.delete(udid);
        return result(0);
      }
      // io <udid> screenshot [--type=png] <path>
      if (sub === "io") {
        if (sim.state !== "Booted") return result(149, "", "Unable to get screenshot: device is not booted\n");
        const target = rest[rest.length - 1];
        if (rest[1] !== "screenshot" || !target) return result(64, "", "mock runner: only `io <udid> screenshot <path>` is supported\n");
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, phonePlaceholderPng(`${sim.udid}:${Math.floor(Date.now() / 5000)}`));
        return result(0, "Detected file type 'PNG' from extension\nWrote screenshot to: " + target + "\n");
      }
    }

    return result(64, "", `mock runner: unsupported simctl command ${sub}\n`);
  }

  private async xcodebuild(args: string[], options: RunOptions): Promise<CommandResult> {
    if (args[0] === "-version") return result(0, "Xcode 16.2\nBuild version 16C5032a\n");

    const value = (flag: string) => {
      const index = args.indexOf(flag);
      return index === -1 ? undefined : args[index + 1];
    };
    const scheme = value("-scheme") ?? "";
    const destination = value("-destination") ?? "";
    const bundle = value("-resultBundlePath");
    const emit = (line: string) => options.onLine?.(line, "stdout");
    const lines: string[] = [];
    const out = (line: string) => {
      lines.push(line);
      emit(line);
      if (options.logFile) fs.appendFileSync(options.logFile, line + "\n");
    };
    const finish = (code: number, extra: Partial<CommandResult> = {}) =>
      result(code, lines.join("\n") + "\n", "", extra);

    if (!scheme) return result(66, "", "xcodebuild: error: Missing -scheme\n");
    if (/missing|nonexistent/i.test(scheme)) {
      const message = `xcodebuild: error: The project named "Demo" does not contain a scheme named "${scheme}". The "-list" option can be used to find the names of the schemes in the project.`;
      out(message);
      return finish(65);
    }

    const udid = /id=([0-9A-Fa-f-]+)/.exec(destination)?.[1]?.toUpperCase();
    const sim = udid ? this.sims.get(udid) : undefined;
    if (!sim || sim.state !== "Booted") {
      out("xcodebuild: error: Unable to find a device matching the provided destination specifier:");
      out(`\t\t{ ${destination} }`);
      return finish(70);
    }

    const shouldFail = /fail/i.test(scheme);
    let flakyFail = false;
    if (/flaky/i.test(scheme)) {
      const key = `${scheme}:${udid}`;
      flakyFail = !this.flakySeen.has(key);
      this.flakySeen.add(key);
    }
    const perTestDelay = /slow/i.test(scheme) ? Math.max(this.latency * 4, 40) : Math.round(this.latency / 4);

    const stamp = () => new Date().toISOString().replace("T", " ").replace(/\.\d+Z$/, " +0000");
    out(`Command line invocation:\n    /usr/bin/xcodebuild test -scheme ${scheme} -destination "${destination}"`);
    out(`Testing started on '${sim.name}'`);
    out(`Test Suite 'All tests' started at ${stamp()}`);

    let failures = 0;
    let total = 0;
    let index = 0;
    for (const suite of SUITES) {
      out(`Test Suite '${suite.className}' started at ${stamp()}`);
      for (const test of suite.tests) {
        const id = `-[${scheme}Tests.${suite.className} ${test}]`;
        out(`Test Case '${id}' started.`);
        await abortableSleep(perTestDelay, options.signal);
        if (options.signal?.aborted) {
          return finish(-1, { aborted: true });
        }
        const duration = (0.005 + (hashOf(id) % 400) / 10000).toFixed(3);
        const fails = (shouldFail && (test === "testInvalidPassword" || test === "testPaymentDeclined")) || (flakyFail && index === 4);
        total += 1;
        index += 1;
        if (fails) {
          failures += 1;
          out(`/Users/dev/Demo/${suite.className}.swift:${40 + total}: error: ${id} : XCTAssertEqual failed: ("Welcome") is not equal to ("Error")`);
          out(`Test Case '${id}' failed (${duration} seconds).`);
        } else {
          out(`Test Case '${id}' passed (${duration} seconds).`);
        }
      }
      out(`Test Suite '${suite.className}' ${failures ? "failed" : "passed"} at ${stamp()}.`);
    }
    out(`\t Executed ${total} tests, with ${failures} failures (0 unexpected) in 0.412 (0.418) seconds`);
    out(failures ? "\n** TEST FAILED **\n" : "\n** TEST SUCCEEDED **\n");

    if (bundle) {
      fs.mkdirSync(bundle, { recursive: true });
      fs.writeFileSync(path.join(bundle, "Info.plist"), "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict/></plist>\n");
    }
    return finish(failures ? 65 : 0);
  }
}
