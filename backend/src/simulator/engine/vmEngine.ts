import { randomUUID } from "node:crypto";
import { EventHub } from "../../core/eventHub";
import { DomainError } from "../../utils/errors";
import { phonePlaceholderPng } from "./pngPlaceholder";

export type VmStatus = "created" | "preparing" | "patching" | "restoring" | "installing" | "booting" | "ready" | "stopped" | "error";

export interface VMConfiguration {
  id: string;
  name: string;
  runtime: string;
  cpu: number;
  memory: number; // GB
  disk: number; // GB
  screen: string;
  variant: string;
  status: VmStatus;
  currentPatchTier: string;
  backupList: string[];
  currentScreen: string;

  // Fault-injection state. Recorded and reported; nothing enforces it on a real guest.
  networkProfile: string;
  thermalThrottle: boolean;
  batteryDegraded: boolean;
  diskFullLevel: number;
  systemClockOffset: number;
}

export interface InputEvent {
  type: "tap" | "swipe" | "keypress" | "scroll";
  x?: number;
  y?: number;
  key?: string;
  duration?: number;
}

const PIPELINE: Array<{ status: VmStatus; action: string; message: string }> = [
  { status: "preparing", action: "fw_prepare", message: "Extracting and merging the IPSW firmware" },
  { status: "patching", action: "fw_patch", message: 'Applying the minimal "boot-only" patch tier' },
  { status: "restoring", action: "restore", message: "Flashing the boot chain and partition image" },
  { status: "installing", action: "cfw_install", message: "Installing custom firmware configuration" },
  { status: "booting", action: "boot", message: "Booting the guest and connecting the control socket" }
];

/**
 * EXPERIMENTAL / SIMULATED. This engine models the lifecycle of a virtualised iOS guest (create,
 * firmware pipeline, boot, backups, input, fault injection) so the API, CLI and dashboard can be
 * developed against it. It does not start a real virtual machine and cannot execute tests, which is
 * why devices it backs report `backend: "simulated"` and `canRunTests: false`.
 */
export class VMEngine {
  private readonly vms = new Map<string, VMConfiguration>();
  private readonly stepDelayMs: number;

  constructor(
    private readonly hub: EventHub,
    initial: VMConfiguration[] = [],
    stepDelayMs = 0
  ) {
    this.stepDelayMs = stepDelayMs;
    for (const vm of initial) this.vms.set(vm.id, vm);
  }

  private require(id: string): VMConfiguration {
    const vm = this.vms.get(id);
    if (!vm) throw new DomainError(`VM not found: ${id}`, 404);
    return vm;
  }

  private emit(vm: VMConfiguration, type: "started" | "log" | "finished", action: string, message: string): void {
    this.hub.emit({ source: "vm", type, action, message: `[simulated] ${message}`, deviceId: vm.id });
  }

  private sleep(): Promise<void> {
    return this.stepDelayMs > 0 ? new Promise((resolve) => setTimeout(resolve, this.stepDelayMs)) : Promise.resolve();
  }

  create(input: { id?: string; name: string; runtime: string; cpu?: number; memory?: number; disk?: number }): VMConfiguration {
    const vm: VMConfiguration = {
      id: input.id ?? randomUUID(),
      name: input.name,
      runtime: input.runtime,
      cpu: input.cpu ?? 4,
      memory: input.memory ?? 4,
      disk: input.disk ?? 32,
      screen: "1170x2532",
      variant: "boot-only",
      status: "created",
      currentPatchTier: "boot-only",
      backupList: ["Clean Install"],
      currentScreen: "Welcome Screen",
      networkProfile: "Wi-Fi",
      thermalThrottle: false,
      batteryDegraded: false,
      diskFullLevel: 10,
      systemClockOffset: 0
    };
    this.vms.set(vm.id, vm);
    this.emit(vm, "finished", "vm_new", `Created VM configuration "${vm.name}" (${vm.cpu} vCPU, ${vm.memory} GB RAM)`);
    return vm;
  }

  async bootPipeline(id: string): Promise<VMConfiguration> {
    const vm = this.require(id);
    for (const step of PIPELINE) {
      vm.status = step.status;
      this.emit(vm, "started", step.action, `[${step.action}] ${step.message}`);
      await this.sleep();
    }
    vm.status = "ready";
    this.emit(vm, "finished", "boot", `VM "${vm.name}" is up`);
    return vm;
  }

  async shutdown(id: string): Promise<VMConfiguration> {
    const vm = this.require(id);
    vm.status = "stopped";
    this.emit(vm, "finished", "vm_shutdown", `VM "${vm.name}" stopped`);
    return vm;
  }

  remove(id: string): void {
    this.vms.delete(id);
  }

  backup(id: string, name: string): VMConfiguration {
    const vm = this.require(id);
    if (!vm.backupList.includes(name)) vm.backupList.push(name);
    this.emit(vm, "finished", "vm_backup", `Recorded state backup "${name}"`);
    return vm;
  }

  restoreBackup(id: string, name: string): VMConfiguration {
    const vm = this.require(id);
    if (!vm.backupList.includes(name)) throw new DomainError(`Backup "${name}" not found for VM ${id}`, 404);
    vm.currentScreen = "Welcome Screen";
    this.emit(vm, "finished", "vm_restore", `Restored state backup "${name}"`);
    return vm;
  }

  switchConfig(id: string, update: { cpu?: number; memory?: number; disk?: number }): VMConfiguration {
    const vm = this.require(id);
    if (update.cpu) vm.cpu = update.cpu;
    if (update.memory) vm.memory = update.memory;
    if (update.disk) vm.disk = update.disk;
    this.emit(vm, "finished", "vm_switch", `Hardware profile is now ${vm.cpu} vCPU / ${vm.memory} GB RAM / ${vm.disk} GB disk`);
    return vm;
  }

  list(): VMConfiguration[] {
    return [...this.vms.values()];
  }

  get(id: string): VMConfiguration | undefined {
    return this.vms.get(id);
  }

  getScreenshot(id: string): { image: string; state: string } {
    const vm = this.require(id);
    return { image: phonePlaceholderPng(`${vm.id}:${vm.currentScreen}`).toString("base64"), state: vm.currentScreen };
  }

  injectInput(id: string, event: InputEvent): { success: boolean; state: string } {
    const vm = this.require(id);
    if (event.type === "tap") vm.currentScreen = `After tap at (${event.x ?? 0}, ${event.y ?? 0})`;
    else if (event.type === "swipe") vm.currentScreen = "After swipe";
    else if (event.type === "scroll") vm.currentScreen = "After scroll";
    else vm.currentScreen = `After typing "${event.key ?? ""}"`;
    this.emit(vm, "log", "vm_input", `Input ${JSON.stringify(event)}`);
    return { success: true, state: vm.currentScreen };
  }

  injectChaos(id: string, chaos: { networkProfile?: string; thermalThrottle?: boolean; systemClockOffset?: number }): VMConfiguration {
    const vm = this.require(id);
    if (chaos.networkProfile !== undefined) vm.networkProfile = chaos.networkProfile;
    if (chaos.thermalThrottle !== undefined) vm.thermalThrottle = chaos.thermalThrottle;
    if (chaos.systemClockOffset !== undefined) vm.systemClockOffset = chaos.systemClockOffset;
    this.emit(vm, "log", "chaos", `Network=${vm.networkProfile}, thermal throttle=${vm.thermalThrottle}, clock offset=${vm.systemClockOffset}s`);
    return vm;
  }

  simulateAging(id: string, aging: { batteryDegraded?: boolean; diskFullLevel?: number }): VMConfiguration {
    const vm = this.require(id);
    if (aging.batteryDegraded !== undefined) vm.batteryDegraded = aging.batteryDegraded;
    if (aging.diskFullLevel !== undefined) vm.diskFullLevel = aging.diskFullLevel;
    this.emit(vm, "log", "device_aging", `Battery degraded=${vm.batteryDegraded}, disk ${vm.diskFullLevel}% full`);
    return vm;
  }

  snapshot(): VMConfiguration[] {
    return this.list();
  }
}
