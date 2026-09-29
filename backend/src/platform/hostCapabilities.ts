import os from "node:os";
import { access } from "node:fs/promises";
import { AppConfig } from "../config/env";
import { readVersion } from "../config/version";
import { CapacitySnapshot } from "../scheduler/policies/capacityPolicy";

export type HostCapabilities = {
  platform: NodeJS.Platform;
  architecture: string;
  kernel: string;
  node: string;
  arm64Linux: boolean;
  kvmDevice: boolean;
  supportedTargets: string[];
};

export async function getHostCapabilities(): Promise<HostCapabilities> {
  const platform = os.platform();
  const architecture = process.arch;
  const arm64Linux = platform === "linux" && architecture === "arm64";

  let kvmDevice = false;
  if (platform === "linux") {
    try {
      await access("/dev/kvm");
      kvmDevice = true;
    } catch {
      kvmDevice = false;
    }
  }

  const supportedTargets: string[] = [];
  if (platform === "darwin") supportedTargets.push("ios-simulator");
  if (arm64Linux) supportedTargets.push("linux-arm64-native");

  return { platform, architecture, kernel: os.release(), node: process.version, arm64Linux, kvmDevice, supportedTargets };
}

export interface BackendCapabilities extends HostCapabilities {
  version: string;
  /** `demo` means simulator commands are simulated: nothing real is started and test results are not real. */
  mode: "live" | "demo";
  modeReason: AppConfig["mockReason"];
  vm: { enabled: boolean; simulated: true; canRunTests: false };
  auth: { required: boolean };
  capacity: CapacitySnapshot;
}

export async function getBackendCapabilities(config: AppConfig, capacity: CapacitySnapshot): Promise<BackendCapabilities> {
  const host = await getHostCapabilities();
  return {
    ...host,
    version: readVersion(),
    mode: config.mock ? "demo" : "live",
    modeReason: config.mockReason,
    vm: { enabled: config.experimentalVm, simulated: true, canRunTests: false },
    auth: { required: !!config.apiToken },
    capacity
  };
}
