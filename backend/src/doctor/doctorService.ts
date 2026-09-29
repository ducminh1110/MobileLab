import fs from "node:fs";
import os from "node:os";
import { AppConfig } from "../config/env";
import { errorMessage } from "../utils/errors";
import { SimulatorEngine } from "../simulator/engine/simulatorEngine";
import { computeMaxLoad } from "../scheduler/policies/capacityPolicy";

export type CheckStatus = "ok" | "warn" | "fail" | "skip";

export interface DoctorCheck {
  id: string;
  name: string;
  status: CheckStatus;
  message: string;
  /** What to do about it. */
  remedy?: string;
}

export interface DoctorReport {
  status: "healthy" | "degraded" | "unhealthy";
  mode: "live" | "demo";
  generatedAt: string;
  checks: DoctorCheck[];
}

const GB = 1024 ** 3;

/**
 * Real environment checks. Nothing here is hard-coded: every result comes from asking the host. In
 * demo mode the Xcode checks are skipped rather than pretending to pass.
 */
export class DoctorService {
  constructor(
    private readonly config: AppConfig,
    private readonly engine: SimulatorEngine
  ) {}

  async run(): Promise<DoctorReport> {
    const checks: DoctorCheck[] = [];
    const add = (check: DoctorCheck) => checks.push(check);
    const demo = this.config.mock;

    if (demo) {
      add({
        id: "mode",
        name: "Execution mode",
        status: "warn",
        message:
          this.config.mockReason === "auto"
            ? `Demo mode: this host is ${os.platform()}, not macOS, so simulator commands are simulated and test results are not real.`
            : "Demo mode (IOSLAB_SIMULATOR_MOCK=true): simulator commands are simulated and test results are not real.",
        remedy: "Run the backend on a Mac with Xcode and unset IOSLAB_SIMULATOR_MOCK to drive real simulators."
      });
    } else {
      add({ id: "mode", name: "Execution mode", status: "ok", message: "Live: driving real Xcode simulators." });
    }

    const skipped = (id: string, name: string): DoctorCheck => ({ id, name, status: "skip", message: "Skipped in demo mode." });

    if (demo) {
      add(skipped("xcode-select", "Xcode command line tools"));
      add(skipped("xcodebuild", "xcodebuild"));
      add(skipped("runtimes", "iOS simulator runtimes"));
      add(skipped("device-types", "Device types"));
    } else {
      if (os.platform() !== "darwin") {
        add({ id: "os", name: "Operating system", status: "fail", message: `iOS simulators need macOS; this host is ${os.platform()}.`, remedy: "Run the backend on a Mac." });
      }
      await this.check(add, "xcode-select", "Xcode command line tools", async () => {
        const result = await this.engine.runner.run("xcode-select", ["-p"], { timeoutMs: 10_000 });
        if (result.code !== 0) throw new Error(result.stderr.trim() || "no developer directory is selected");
        return `Developer directory: ${result.stdout.trim()}`;
      }, "Install Xcode, then run: sudo xcode-select -s /Applications/Xcode.app/Contents/Developer");

      await this.check(add, "xcodebuild", "xcodebuild", async () => this.engine.xcodebuild.version(), "Open Xcode once to accept its license, or run: sudo xcodebuild -license accept");

      await this.check(add, "runtimes", "iOS simulator runtimes", async () => {
        const runtimes = await this.engine.simctl.runtimes();
        if (runtimes.length === 0) throw new Error("no iOS runtimes are installed");
        return `${runtimes.length} installed: ${runtimes.map((r) => r.name).join(", ")}`;
      }, "Install one in Xcode > Settings > Components (or: xcodebuild -downloadPlatform iOS).");

      await this.check(add, "device-types", "Device types", async () => {
        const types = await this.engine.simctl.deviceTypes();
        if (types.length === 0) throw new Error("no iPhone or iPad device types found");
        return `${types.length} available`;
      });
    }

    this.checkDataDir(add);
    this.checkCapacity(add);

    // In demo mode everything is simulated anyway; only call it out when someone switched it on for real.
    if (this.config.experimentalVm && !this.config.mock) {
      add({
        id: "vm",
        name: "Experimental VM backend",
        status: "warn",
        message: "Enabled, but simulated: it does not start a real VM and cannot run tests.",
        remedy: "Unset IOSLAB_EXPERIMENTAL_VM unless you are developing against the VM API."
      });
    }

    if (!this.config.apiToken && !["127.0.0.1", "localhost", "::1"].includes(this.config.host)) {
      add({
        id: "auth",
        name: "API access",
        status: "warn",
        message: `Listening on ${this.config.host} without IOSLAB_API_TOKEN: anyone who can reach this port can run commands on this machine.`,
        remedy: "Set IOSLAB_API_TOKEN, or bind to 127.0.0.1 (the default)."
      });
    }

    const status = checks.some((c) => c.status === "fail") ? "unhealthy" : checks.some((c) => c.status === "warn") ? "degraded" : "healthy";
    return { status, mode: demo ? "demo" : "live", generatedAt: new Date().toISOString(), checks };
  }

  private async check(add: (c: DoctorCheck) => void, id: string, name: string, probe: () => Promise<string>, remedy?: string): Promise<void> {
    try {
      add({ id, name, status: "ok", message: await probe() });
    } catch (error) {
      add({ id, name, status: "fail", message: errorMessage(error), remedy });
    }
  }

  private checkDataDir(add: (c: DoctorCheck) => void): void {
    try {
      fs.mkdirSync(this.config.dataDir, { recursive: true });
      fs.accessSync(this.config.dataDir, fs.constants.W_OK);
    } catch (error) {
      add({ id: "data-dir", name: "Data directory", status: "fail", message: `${this.config.dataDir} is not writable: ${errorMessage(error)}`, remedy: "Set IOSLAB_DATA_DIR to a writable folder." });
      return;
    }
    let freeGb: number | undefined;
    try {
      const stat = fs.statfsSync(this.config.dataDir);
      freeGb = (stat.bavail * stat.bsize) / GB;
    } catch {
      /* statfs unavailable */
    }
    if (freeGb === undefined) {
      add({ id: "data-dir", name: "Data directory", status: "ok", message: this.config.dataDir });
    } else if (freeGb < 2) {
      add({ id: "data-dir", name: "Data directory", status: "fail", message: `${freeGb.toFixed(1)} GB free in ${this.config.dataDir}`, remedy: "Free up disk space; simulators and test results need room." });
    } else if (freeGb < 10) {
      add({ id: "data-dir", name: "Data directory", status: "warn", message: `Only ${freeGb.toFixed(1)} GB free in ${this.config.dataDir}`, remedy: "Run cleanup or free up disk space." });
    } else {
      add({ id: "data-dir", name: "Data directory", status: "ok", message: `${this.config.dataDir} (${freeGb.toFixed(0)} GB free)` });
    }
  }

  private checkCapacity(add: (c: DoctorCheck) => void): void {
    const max = computeMaxLoad(this.config.maxLoad);
    const memoryGb = os.totalmem() / GB;
    add({
      id: "capacity",
      name: "Capacity",
      status: memoryGb < 8 ? "warn" : "ok",
      message: `Up to ${max} simulator${max === 1 ? "" : "s"} at once (${os.cpus().length} cores, ${memoryGb.toFixed(0)} GB RAM${this.config.maxLoad ? ", set by IOSLAB_MAX_LOAD" : ""}).`,
      remedy: memoryGb < 8 ? "Simulators are memory hungry; expect slowdowns with less than 8 GB." : undefined
    });
  }
}
