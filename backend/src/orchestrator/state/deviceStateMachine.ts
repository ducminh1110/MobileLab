import { DeviceStatus } from "../../simulator/models/types";
import { DomainError } from "../../utils/errors";

const transitions: Record<DeviceStatus, DeviceStatus[]> = {
  created: ["booting", "stopped", "error"],
  booting: ["ready", "error"],
  ready: ["busy", "shutting_down", "error"],
  busy: ["ready", "error"],
  shutting_down: ["stopped", "error"],
  stopped: ["booting", "error"],
  error: ["stopped", "booting", "shutting_down"]
};

export function canTransitionDevice(from: DeviceStatus, to: DeviceStatus): boolean {
  return transitions[from]?.includes(to) ?? false;
}

export function transitionDeviceState(from: DeviceStatus, to: DeviceStatus): DeviceStatus {
  if (!canTransitionDevice(from, to)) {
    throw new DomainError(`Invalid device transition: ${from} -> ${to}`, 409);
  }
  return to;
}
