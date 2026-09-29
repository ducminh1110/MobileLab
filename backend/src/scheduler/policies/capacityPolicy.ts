import os from "node:os";
import { Device, DeviceStatus } from "../../simulator/models/types";

export const COST_WEIGHTS = {
  simulator: 1,
  vm: 4
} as const;

/** A booted device occupies RAM and CPU even while idle, so it counts against capacity. */
const LOADED_STATUSES: readonly DeviceStatus[] = ["booting", "ready", "busy", "shutting_down"];

export interface CapacitySnapshot {
  /** Total cost the host can carry. One simulator costs 1. */
  maxLoad: number;
  /** Cost of everything currently booted. */
  load: number;
  cpuCores: number;
  memoryGb: number;
}

/** Roughly one simulator per core, and never more than one per 2 GB of RAM. `override` wins. */
export function computeMaxLoad(override?: number): number {
  if (override) return override;
  const cpuCores = os.cpus().length;
  const memoryGb = os.totalmem() / 1024 ** 3;
  return Math.max(1, Math.floor(Math.min(cpuCores, memoryGb / 2)));
}

export function getDeviceCost(device: Pick<Device, "type">): number {
  return device.type === "vm" ? COST_WEIGHTS.vm : COST_WEIGHTS.simulator;
}

export function getActiveLoad(devices: Array<Pick<Device, "type" | "status">>): number {
  return devices.filter((d) => LOADED_STATUSES.includes(d.status)).reduce((sum, d) => sum + getDeviceCost(d), 0);
}

export function getCapacitySnapshot(devices: Array<Pick<Device, "type" | "status">>, override?: number): CapacitySnapshot {
  return {
    maxLoad: computeMaxLoad(override),
    load: getActiveLoad(devices),
    cpuCores: os.cpus().length,
    memoryGb: Math.round((os.totalmem() / 1024 ** 3) * 10) / 10
  };
}
