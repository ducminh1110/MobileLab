import path from "node:path";
import { DomainError } from "../../utils/errors";
import { CommandError, CommandResult, CommandRunner, ensureSuccess } from "./commandRunner";
import { SimulatorDevice } from "../models/types";

export interface CatalogRuntime {
  identifier: string;
  name: string;
  version: string;
  /** Device type identifiers this runtime can host. Absent when simctl does not say. */
  supportedDeviceTypes?: string[];
}

export interface CatalogDeviceType {
  identifier: string;
  name: string;
  family: string;
}

const UDID = /^[0-9A-Fa-f]{8}(-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}$/;

/** simctl exits 149 with "... in current state: X" when a device is already where we want it. */
function alreadyInState(result: CommandResult, state: string): boolean {
  return result.code === 149 && new RegExp(`current state: ${state}`, "i").test(`${result.stderr}${result.stdout}`);
}

function compareVersions(a: string, b: string): number {
  const pa = a.split(".").map(Number);
  const pb = b.split(".").map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i += 1) {
    const diff = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (diff !== 0) return diff;
  }
  return 0;
}

export class SimctlClient {
  constructor(private readonly runner: CommandRunner) {}

  private async simctl(args: string[], timeoutMs = 60_000): Promise<CommandResult> {
    return this.runner.run("xcrun", ["simctl", ...args], { timeoutMs });
  }

  async create(name: string, deviceType: string, runtime: string): Promise<{ udid: string }> {
    const result = ensureSuccess("simctl create", await this.simctl(["create", name, deviceType, runtime], 30_000));
    const udid = result.stdout.trim().split("\n").pop()?.trim() ?? "";
    if (!UDID.test(udid)) {
      throw new CommandError(`simctl create returned an unexpected identifier: "${udid}"`, "simctl create", result);
    }
    return { udid };
  }

  /** Boots the simulator and waits until it has finished booting. Booting a booted device is a no-op. */
  async boot(udid: string): Promise<void> {
    const boot = await this.simctl(["boot", udid], 120_000);
    if (!alreadyInState(boot, "Booted")) ensureSuccess("simctl boot", boot);
    ensureSuccess("simctl bootstatus", await this.simctl(["bootstatus", udid], 180_000));
  }

  /** Shuts the simulator down. Shutting down a stopped device is a no-op. */
  async shutdown(udid: string): Promise<void> {
    const result = await this.simctl(["shutdown", udid], 60_000);
    if (!alreadyInState(result, "Shutdown")) ensureSuccess("simctl shutdown", result);
  }

  async erase(udid: string): Promise<void> {
    ensureSuccess("simctl erase", await this.simctl(["erase", udid], 60_000));
  }

  /** Deletes the simulator from the host. A device that is already gone is fine. */
  async delete(udid: string): Promise<void> {
    const result = await this.simctl(["delete", udid], 60_000);
    if (result.code !== 0 && /invalid device/i.test(result.stderr)) return;
    ensureSuccess("simctl delete", result);
  }

  async screenshot(udid: string, filePath: string): Promise<void> {
    ensureSuccess("simctl screenshot", await this.simctl(["io", udid, "screenshot", "--type=png", path.resolve(filePath)], 30_000));
  }

  async list(): Promise<SimulatorDevice[]> {
    const result = ensureSuccess("simctl list", await this.simctl(["list", "devices", "--json"], 30_000));
    const parsed = JSON.parse(result.stdout) as {
      devices: Record<string, Array<{ udid: string; name: string; state: string; isAvailable: boolean; deviceTypeIdentifier?: string }>>;
    };
    return Object.entries(parsed.devices).flatMap(([runtime, devices]) => devices.map((device) => ({ ...device, runtime })));
  }

  async runtimes(): Promise<CatalogRuntime[]> {
    const result = ensureSuccess("simctl list runtimes", await this.simctl(["list", "runtimes", "--json"], 30_000));
    const parsed = JSON.parse(result.stdout) as {
      runtimes: Array<{
        identifier: string;
        name: string;
        version: string;
        platform?: string;
        isAvailable?: boolean;
        supportedDeviceTypes?: Array<{ identifier: string }>;
      }>;
    };
    return parsed.runtimes
      .filter((r) => r.isAvailable !== false && (r.platform ? r.platform === "iOS" : r.identifier.includes(".iOS-")))
      .map((r) => ({
        identifier: r.identifier,
        name: r.name,
        version: r.version,
        supportedDeviceTypes: r.supportedDeviceTypes?.map((d) => d.identifier)
      }))
      .sort((a, b) => compareVersions(b.version, a.version));
  }

  async deviceTypes(): Promise<CatalogDeviceType[]> {
    const result = ensureSuccess("simctl list devicetypes", await this.simctl(["list", "devicetypes", "--json"], 30_000));
    const parsed = JSON.parse(result.stdout) as {
      devicetypes: Array<{ identifier: string; name: string; productFamily?: string }>;
    };
    return parsed.devicetypes
      .filter((d) => d.productFamily === "iPhone" || d.productFamily === "iPad" || /iPhone|iPad/.test(d.name))
      .map((d) => ({ identifier: d.identifier, name: d.name, family: d.productFamily ?? (d.name.startsWith("iPad") ? "iPad" : "iPhone") }));
  }
}

export interface Catalog {
  runtimes: CatalogRuntime[];
  deviceTypes: CatalogDeviceType[];
}

const CACHE_MS = 30_000;

/**
 * Knows which runtimes and device types this host can actually create, and turns the shorthand people
 * type ("17.5", "iOS 18", "iPhone 15") into the identifiers simctl requires. Without this every caller
 * had to hard-code `com.apple.CoreSimulator.SimRuntime.iOS-18-0`, which simply fails on any machine
 * that has a different Xcode.
 */
export class CatalogService {
  private cache?: { at: number; value: Catalog };

  constructor(private readonly simctl: SimctlClient) {}

  async get(force = false): Promise<Catalog> {
    if (!force && this.cache && Date.now() - this.cache.at < CACHE_MS) return this.cache.value;
    let value: Catalog;
    try {
      const [runtimes, deviceTypes] = await Promise.all([this.simctl.runtimes(), this.simctl.deviceTypes()]);
      value = { runtimes, deviceTypes };
    } catch (error) {
      throw new DomainError(`Cannot read the simulator catalog: ${error instanceof Error ? error.message : String(error)}`, 503);
    }
    this.cache = { at: Date.now(), value };
    return value;
  }

  /** Whether `runtime` can host `model`. Unknown compatibility is treated as compatible. */
  static supports(runtime: CatalogRuntime, model: CatalogDeviceType): boolean {
    return !runtime.supportedDeviceTypes?.length || runtime.supportedDeviceTypes.includes(model.identifier);
  }

  /**
   * Resolves a runtime. With no input it picks the newest one; when `model` is given it picks the newest
   * runtime that can actually host that device type (a new iPhone does not exist on an old iOS).
   */
  async resolveRuntime(input?: string, model?: CatalogDeviceType): Promise<CatalogRuntime> {
    const { runtimes } = await this.get();
    if (runtimes.length === 0) {
      throw new DomainError("No iOS simulator runtimes are installed. Install one in Xcode > Settings > Components.", 409);
    }
    if (!input) {
      const compatible = model ? runtimes.filter((r) => CatalogService.supports(r, model)) : runtimes;
      if (compatible.length === 0) throw new DomainError(`No installed runtime supports ${model!.name}.`, 409);
      return compatible[0];
    }

    const wanted = input.trim().toLowerCase();
    const digits = wanted.replace(/^ios[\s-]*/, "").replace(/-/g, ".");
    const found =
      runtimes.find((r) => r.identifier.toLowerCase() === wanted) ??
      runtimes.find((r) => r.name.toLowerCase() === wanted) ??
      runtimes.find((r) => r.version === digits) ??
      // "18" matches the newest 18.x
      runtimes.find((r) => r.version.split(".")[0] === digits);
    if (!found) {
      throw new DomainError(`Unknown runtime "${input}". Available: ${runtimes.map((r) => r.name).join(", ")}`, 400);
    }
    return found;
  }

  /**
   * Resolves a device type. When `runtime` is given the choice is limited to what that runtime supports:
   * asking for something it cannot host is a 400 that says so, and the default is the newest plain iPhone
   * it does support.
   */
  async resolveDeviceType(input?: string, runtime?: CatalogRuntime): Promise<CatalogDeviceType> {
    const { deviceTypes } = await this.get();
    if (deviceTypes.length === 0) throw new DomainError("No iPhone/iPad device types are available on this host.", 409);

    const pool = runtime?.supportedDeviceTypes?.length ? deviceTypes.filter((d) => runtime.supportedDeviceTypes!.includes(d.identifier)) : deviceTypes;
    if (pool.length === 0) throw new DomainError(`${runtime!.name} does not list any iPhone or iPad device types.`, 409);

    if (!input) {
      // simctl lists device types oldest first, so search from the end for the newest plain iPhone.
      const newestFirst = [...pool].reverse();
      return newestFirst.find((d) => /^iPhone \d+$/.test(d.name)) ?? newestFirst.find((d) => d.family === "iPhone") ?? pool[0];
    }

    const wanted = input.trim().toLowerCase();
    const match = (list: CatalogDeviceType[]) =>
      list.find((d) => d.identifier.toLowerCase() === wanted) ??
      list.find((d) => d.name.toLowerCase() === wanted) ??
      list.find((d) => d.identifier.toLowerCase().endsWith(`.${wanted.replace(/\s+/g, "-")}`));

    const found = match(pool);
    if (found) return found;
    const elsewhere = match(deviceTypes);
    if (elsewhere && runtime) {
      throw new DomainError(`${elsewhere.name} is not available on ${runtime.name}. Available there: ${pool.map((d) => d.name).join(", ")}`, 400);
    }
    throw new DomainError(`Unknown device type "${input}". Available: ${deviceTypes.map((d) => d.name).join(", ")}`, 400);
  }
}
