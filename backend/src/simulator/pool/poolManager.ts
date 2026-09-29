import { Device } from "../models/types";

export class PoolManager {
  private readonly devices = new Map<string, Device>();

  put(device: Device): void {
    this.devices.set(device.id, device);
  }

  get(id: string): Device | undefined {
    return this.devices.get(id);
  }

  list(): Device[] {
    return [...this.devices.values()];
  }

  update(device: Device): void {
    this.devices.set(device.id, device);
  }

  remove(id: string): boolean {
    return this.devices.delete(id);
  }
}
