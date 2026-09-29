import { randomUUID } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { ArtifactRecord, ArtifactService } from "../../artifacts/artifactService";
import { AppConfig } from "../../config/env";
import { EventHub } from "../../core/eventHub";
import { Metrics } from "../../metrics/prometheus";
import { WebhookNotifier } from "../../notify/webhook";
import { CapacitySnapshot, COST_WEIGHTS, getCapacitySnapshot } from "../../scheduler/policies/capacityPolicy";
import { backoffMs, describeRequirements, deviceMatchesJob, isTestCapable } from "../../scheduler/services/schedulerService";
import { CommandResult } from "../../simulator/engine/commandRunner";
import { CatalogDeviceType, CatalogRuntime, CatalogService } from "../../simulator/engine/simctlClient";
import { SimulatorEngine } from "../../simulator/engine/simulatorEngine";
import { ParsedTestRun, TestResultParser, toJUnitXml } from "../../simulator/engine/testResultParser";
import { VMEngine } from "../../simulator/engine/vmEngine";
import { Device, DeviceStatus, DeviceType, isTerminalJobStatus, JobStatus, RunStatus, TestJob, TestRun, TestRunView } from "../../simulator/models/types";
import { PoolManager } from "../../simulator/pool/poolManager";
import { PersistedState, StateStore } from "../../store/stateStore";
import { DomainError, errorMessage } from "../../utils/errors";
import { transitionDeviceState } from "../state/deviceStateMachine";
import { transitionJobState } from "../state/jobStateMachine";

export interface OrchestratorDeps {
  config: AppConfig;
  hub: EventHub;
  engine: SimulatorEngine;
  vmEngine: VMEngine;
  store: StateStore;
  artifacts: ArtifactService;
  metrics?: Metrics;
  webhook?: WebhookNotifier;
}

export interface SpawnDeviceInput {
  name?: string;
  runtime?: string;
  modelId?: string;
  type?: DeviceType;
  cpu?: number;
  memory?: number;
  disk?: number;
  /** Resolve once the device is ready (default) instead of as soon as it starts booting. */
  wait?: boolean;
}

export interface EnqueueTestInput {
  testTarget: string;
  projectPath?: string;
  workspacePath?: string;
  workingDirectory?: string;
  configuration?: string;
  onlyTesting?: string[];
  maxRetries?: number;
  requiredRuntime?: string;
  requiredModelId?: string;
  autoProvision?: boolean;
}

export interface CreateRunInput extends Omit<EnqueueTestInput, "requiredRuntime" | "requiredModelId"> {
  name?: string;
  runtimes?: string[];
  models?: string[];
  maxParallel?: number;
}

export interface JobFilter {
  status?: JobStatus;
  runId?: string;
  limit?: number;
}

const MAX_MATRIX_JOBS = 64;

function nowIso(): string {
  return new Date().toISOString();
}

export class OrchestratorService {
  private readonly pool = new PoolManager();
  private readonly jobs = new Map<string, TestJob>();
  private readonly runs = new Map<string, TestRun>();
  private readonly running = new Map<string, { controller: AbortController; done: Promise<void> }>();
  private readonly retryTimers = new Map<string, NodeJS.Timeout>();
  private readonly waiters = new Map<string, Array<(job: TestJob) => void>>();
  /** In-flight lifecycle operation per device (boot, shutdown, delete, provisioning). */
  private readonly deviceOps = new Map<string, Promise<unknown>>();
  /** Ephemeral device -> the job that caused it to be created. */
  private readonly provisionedFor = new Map<string, string>();
  private readonly provisionFailures = new Map<string, number>();
  private readonly cancelRequested = new Set<string>();
  private closed = false;
  private cpuSample = { at: Date.now(), usage: process.cpuUsage() };

  readonly vmEngine: VMEngine;
  readonly hub: EventHub;
  readonly artifacts: ArtifactService;
  private readonly config: AppConfig;
  private readonly engine: SimulatorEngine;
  private readonly store: StateStore;
  private readonly metrics?: Metrics;
  private readonly webhook?: WebhookNotifier;

  constructor(
    deps: OrchestratorDeps,
    private readonly initial: PersistedState
  ) {
    this.config = deps.config;
    this.hub = deps.hub;
    this.engine = deps.engine;
    this.vmEngine = deps.vmEngine;
    this.store = deps.store;
    this.artifacts = deps.artifacts;
    this.metrics = deps.metrics;
    this.webhook = deps.webhook;
    this.metrics?.bind({ listDevices: () => this.listDevices(), listJobs: () => [...this.jobs.values()], capacity: () => this.capacity() });
  }

  // ------------------------------------------------------------------ lifecycle

  /** Restores persisted state and reconciles it with what actually exists on the host. */
  async initialize(): Promise<void> {
    for (const device of this.initial.devices) this.pool.put(device);
    for (const job of this.initial.jobs) this.jobs.set(job.id, job);
    for (const run of this.initial.runs) this.runs.set(run.id, run);

    for (const job of [...this.jobs.values()]) {
      if (job.status === "running") {
        this.finalizeJob(job.id, "failed", { error: "Interrupted: the backend restarted while this job was running. Re-run it to try again." });
      } else if (job.status === "retrying") {
        this.jobs.set(job.id, { ...job, status: "queued", updatedAt: nowIso() });
      }
    }

    for (const device of this.pool.list()) {
      if (device.type === "vm") {
        const vm = this.vmEngine.get(device.id);
        if (device.status !== "ready" && device.status !== "stopped" && device.status !== "error") {
          this.pool.update({ ...device, status: vm?.status === "ready" ? "ready" : "stopped" });
        }
      } else if (device.status === "busy") {
        this.pool.update({ ...device, status: "ready", currentJobId: undefined });
      }
    }

    await this.syncDevices();

    // Ephemeral simulators only exist to serve queued work; anything left over from a previous run is garbage.
    for (const device of this.pool.list()) {
      if (device.ephemeral && device.status !== "busy") {
        void this.retireDevice(device).catch((error) => this.reportDeviceError(device.id, error));
      }
    }

    if (this.config.retentionDays > 0) this.cleanup(this.config.retentionDays);
    this.persist();
    this.pump();
  }

  /** Stops accepting work, cancels what is running and writes state to disk. */
  async shutdown(): Promise<void> {
    this.closed = true;
    for (const timer of this.retryTimers.values()) clearTimeout(timer);
    this.retryTimers.clear();
    for (const [jobId, entry] of this.running) {
      this.cancelRequested.add(jobId);
      entry.controller.abort();
    }
    await Promise.race([Promise.allSettled([...this.running.values()].map((r) => r.done)), new Promise((resolve) => setTimeout(resolve, 8_000))]);
    await Promise.allSettled([...this.deviceOps.values()]);
    this.persistNow();
  }

  private snapshot(): PersistedState {
    return {
      version: 1,
      devices: this.pool.list(),
      jobs: [...this.jobs.values()],
      runs: [...this.runs.values()],
      artifacts: this.artifacts.snapshot(),
      vms: this.vmEngine.snapshot()
    };
  }

  private persist(): void {
    this.store.scheduleSave(() => this.snapshot());
  }

  private persistNow(): void {
    this.store.scheduleSave(() => this.snapshot());
    this.store.flush();
  }

  // ------------------------------------------------------------------ capacity

  capacity(): CapacitySnapshot {
    return getCapacitySnapshot(this.pool.list(), this.config.maxLoad);
  }

  // ------------------------------------------------------------------ devices

  private view(device: Device): Device {
    if (device.type !== "vm") return device;
    const vm = this.vmEngine.get(device.id);
    if (!vm) return device;
    return {
      ...device,
      variant: vm.variant,
      currentPatchTier: vm.currentPatchTier,
      backupList: [...vm.backupList],
      cpu: vm.cpu,
      memory: vm.memory,
      disk: vm.disk,
      screen: vm.screen
    };
  }

  listDevices(): Device[] {
    return this.pool.list().map((device) => this.view(device));
  }

  getDevice(id: string): Device | undefined {
    const device = this.pool.get(id);
    return device ? this.view(device) : undefined;
  }

  private requireDevice(id: string): Device {
    const device = this.pool.get(id);
    if (!device) throw new DomainError(`Device not found: ${id}`, 404);
    return device;
  }

  private assertNoOperation(device: Device): void {
    if (this.deviceOps.has(device.id)) {
      throw new DomainError(`"${device.name}" is still ${device.status === "shutting_down" ? "shutting down" : "starting"}. Try again in a moment.`, 409);
    }
  }

  private patchDevice(id: string, patch: Partial<Device>, announce?: string): Device | undefined {
    const current = this.pool.get(id);
    if (!current) return undefined;
    const next: Device = { ...current, ...patch, updatedAt: nowIso() };
    this.pool.update(next);
    this.persist();
    if (announce) {
      this.hub.emit({ source: "orchestrator", type: patch.status === "error" ? "error" : "log", action: "device_status", message: announce, deviceId: id });
    }
    return next;
  }

  private reportDeviceError(id: string, error: unknown): void {
    this.patchDevice(id, { status: "error", lastError: errorMessage(error) }, `${this.pool.get(id)?.name ?? id}: ${errorMessage(error)}`);
  }

  private track<T>(deviceId: string, op: Promise<T>): Promise<T> {
    const tracked = op.finally(() => {
      if (this.deviceOps.get(deviceId) === tracked) this.deviceOps.delete(deviceId);
    });
    this.deviceOps.set(deviceId, tracked);
    return tracked;
  }

  async spawnDevice(input: SpawnDeviceInput): Promise<Device> {
    const type = input.type ?? "simulator";
    const cost = type === "vm" ? COST_WEIGHTS.vm : COST_WEIGHTS.simulator;
    const capacity = this.capacity();
    if (capacity.load + cost > capacity.maxLoad) {
      throw new DomainError(
        `Capacity exceeded: ${capacity.load} of ${capacity.maxLoad} units in use and this device needs ${cost}. Shut down or delete a device first, or raise IOSLAB_MAX_LOAD.`,
        429
      );
    }

    if (type === "vm") return this.spawnVm(input);

    const { runtime, model } = await this.resolveTarget(input.runtime, input.modelId);
    const device = this.createSimulator({ name: input.name?.trim() || `${model.name} (${runtime.name})`, runtime, model, ephemeral: false });

    if (input.wait === false) return this.view(device);

    const ok = await this.deviceOps.get(device.id);
    if (!ok) {
      const failed = this.pool.get(device.id);
      this.pool.remove(device.id);
      this.persist();
      throw new DomainError(`Could not start the simulator: ${failed?.lastError ?? "unknown error"}`, 502);
    }
    return this.view(this.pool.get(device.id)!);
  }

  private async spawnVm(input: SpawnDeviceInput): Promise<Device> {
    if (!this.config.experimentalVm) {
      throw new DomainError("The experimental VM backend is disabled. Set IOSLAB_EXPERIMENTAL_VM=1 to enable it (note: it is simulated and cannot run tests).", 501);
    }
    const name = input.name?.trim() || "iOS VM";
    const vm = this.vmEngine.create({ name, runtime: input.runtime ?? "simulated", cpu: input.cpu, memory: input.memory, disk: input.disk });
    const now = nowIso();
    const device: Device = {
      id: vm.id,
      name,
      runtime: vm.runtime,
      status: "booting",
      type: "vm",
      backend: "simulated",
      canRunTests: false,
      ephemeral: false,
      createdAt: now,
      updatedAt: now
    };
    this.pool.put(device);
    this.persist();
    this.hub.emit({ source: "orchestrator", type: "started", action: "spawn_device", message: `Creating VM "${name}"`, deviceId: device.id });

    const op = this.track(
      device.id,
      this.vmEngine
        .bootPipeline(device.id)
        .then(() => {
          this.patchDevice(device.id, { status: "ready" }, `VM "${name}" is ready`);
          return true;
        })
        .catch((error) => {
          this.reportDeviceError(device.id, error);
          return false;
        })
    );
    if (input.wait === false) return this.view(this.pool.get(device.id)!);
    await op;
    return this.view(this.pool.get(device.id)!);
  }

  private createSimulator(opts: { name: string; runtime: CatalogRuntime; model: CatalogDeviceType; ephemeral: boolean }): Device {
    const now = nowIso();
    const device: Device = {
      id: randomUUID(),
      name: opts.name,
      runtime: opts.runtime.identifier,
      runtimeName: opts.runtime.name,
      modelId: opts.model.identifier,
      modelName: opts.model.name,
      status: "booting",
      type: "simulator",
      backend: "simctl",
      canRunTests: true,
      ephemeral: opts.ephemeral,
      createdAt: now,
      updatedAt: now
    };
    this.pool.put(device);
    this.persist();
    this.hub.emit({ source: "orchestrator", type: "started", action: "spawn_device", message: `Creating ${device.name}`, deviceId: device.id });
    this.track(device.id, this.bringUpSimulator(device.id));
    return device;
  }

  /** Creates (if needed) and boots the simulator behind a device record. Never rejects: failures land on the device. */
  private async bringUpSimulator(deviceId: string): Promise<boolean> {
    const device = this.pool.get(deviceId);
    if (!device || !device.modelId) return false;
    let udid = device.simulatorUdid;
    try {
      if (!udid) {
        udid = (await this.engine.simctl.create(device.name, device.modelId, device.runtime)).udid;
        this.patchDevice(deviceId, { simulatorUdid: udid });
      }
      await this.engine.simctl.boot(udid);
      this.patchDevice(deviceId, { status: "ready", lastError: undefined }, `${device.name} is ready`);
      this.pump();
      return true;
    } catch (error) {
      if (udid) {
        // Don't leave half-created simulators behind on the host.
        await this.engine.simctl.delete(udid).catch(() => undefined);
        this.patchDevice(deviceId, { simulatorUdid: undefined });
      }
      this.reportDeviceError(deviceId, error);
      this.onProvisionFailed(deviceId, error);
      this.pump();
      return false;
    }
  }

  private onProvisionFailed(deviceId: string, error: unknown): void {
    const jobId = this.provisionedFor.get(deviceId);
    if (!jobId) return;
    this.provisionedFor.delete(deviceId);
    const failures = (this.provisionFailures.get(jobId) ?? 0) + 1;
    this.provisionFailures.set(jobId, failures);
    const device = this.pool.get(deviceId);
    if (device?.ephemeral) this.pool.remove(deviceId);
    const job = this.jobs.get(jobId);
    if (job && job.status === "queued" && failures >= 2) {
      this.finalizeJob(jobId, "failed", { error: `Could not create a simulator for this job: ${errorMessage(error)}` });
    }
  }

  async bootDevice(id: string): Promise<Device> {
    const device = this.requireDevice(id);
    this.assertNoOperation(device);
    if (device.status === "ready") return this.view(device);
    if (device.status === "busy") throw new DomainError(`"${device.name}" is running a job.`, 409);
    transitionDeviceState(device.status, "booting");
    this.patchDevice(id, { status: "booting", lastError: undefined }, `Booting ${device.name}`);

    const op =
      device.type === "vm"
        ? this.vmEngine
            .bootPipeline(id)
            .then(() => {
              this.patchDevice(id, { status: "ready" }, `VM "${device.name}" is ready`);
              return true;
            })
            .catch((error) => {
              this.reportDeviceError(id, error);
              return false;
            })
        : this.bringUpSimulator(id);
    const ok = await this.track(id, op);
    const after = this.requireDevice(id);
    if (!ok) throw new DomainError(`Could not boot "${after.name}": ${after.lastError ?? "unknown error"}`, 502);
    if (device.type === "vm") this.pump();
    return this.view(after);
  }

  async shutdownDevice(id: string): Promise<Device> {
    const device = this.requireDevice(id);
    this.assertNoOperation(device);
    if (device.status === "stopped") return this.view(device);
    if (device.status === "busy") throw new DomainError(`"${device.name}" is running a job. Cancel the job first.`, 409);
    transitionDeviceState(device.status, "shutting_down");
    this.patchDevice(id, { status: "shutting_down" }, `Shutting down ${device.name}`);

    const op = (async () => {
      try {
        if (device.type === "vm") await this.vmEngine.shutdown(id);
        else if (device.simulatorUdid) await this.engine.simctl.shutdown(device.simulatorUdid);
        this.patchDevice(id, { status: "stopped", lastError: undefined }, `${device.name} stopped`);
        return true;
      } catch (error) {
        this.reportDeviceError(id, error);
        return false;
      }
    })();
    const ok = await this.track(id, op);
    if (!ok) throw new DomainError(`Could not shut down "${device.name}": ${this.pool.get(id)?.lastError}`, 502);
    this.pump();
    return this.view(this.requireDevice(id));
  }

  async deleteDevice(id: string): Promise<void> {
    const device = this.requireDevice(id);
    this.assertNoOperation(device);
    if (device.status === "busy") throw new DomainError(`"${device.name}" is running a job. Cancel the job first.`, 409);
    try {
      await this.retireDevice(device);
    } catch (error) {
      throw new DomainError(`Could not delete "${device.name}": ${errorMessage(error)}`, 502);
    }
  }

  /** Shuts a device down, removes it from the host and from the pool. */
  private retireDevice(device: Device): Promise<void> {
    if (this.deviceOps.has(device.id)) return Promise.resolve();
    this.patchDevice(device.id, { status: "shutting_down" }, `Removing ${device.name}`);
    const op = (async () => {
      try {
        if (device.type === "vm") {
          await this.vmEngine.shutdown(device.id).catch(() => undefined);
          this.vmEngine.remove(device.id);
        } else if (device.simulatorUdid) {
          await this.engine.simctl.shutdown(device.simulatorUdid);
          await this.engine.simctl.delete(device.simulatorUdid);
        }
        this.pool.remove(device.id);
        this.persist();
        this.hub.emit({ source: "orchestrator", type: "finished", action: "device_removed", message: `Removed ${device.name}`, deviceId: device.id });
      } catch (error) {
        this.reportDeviceError(device.id, error);
        throw error;
      } finally {
        this.pump();
      }
    })();
    return this.track(device.id, op);
  }

  /** Brings the pool in line with what `simctl` reports (devices deleted or shut down outside MobileLab). */
  async syncDevices(): Promise<{ removed: number; updated: number }> {
    if (this.pool.list().every((d) => d.type !== "simulator")) return { removed: 0, updated: 0 };
    let host;
    try {
      host = await this.engine.simctl.list();
    } catch (error) {
      this.hub.emit({ source: "orchestrator", type: "error", action: "sync_devices", message: `Could not read simulators from the host: ${errorMessage(error)}` });
      return { removed: 0, updated: 0 };
    }
    const byUdid = new Map(host.map((d) => [d.udid.toUpperCase(), d]));
    let removed = 0;
    let updated = 0;

    for (const device of this.pool.list()) {
      if (device.type !== "simulator" || this.deviceOps.has(device.id) || this.running.has(device.currentJobId ?? "")) continue;
      const actual = device.simulatorUdid ? byUdid.get(device.simulatorUdid.toUpperCase()) : undefined;
      if (!actual) {
        if (device.status !== "error" || device.simulatorUdid) {
          this.pool.remove(device.id);
          removed += 1;
          this.hub.emit({ source: "orchestrator", type: "log", action: "device_removed", message: `${device.name} no longer exists on this host`, deviceId: device.id });
        }
        continue;
      }
      const status: DeviceStatus | undefined = actual.state === "Booted" ? "ready" : actual.state === "Shutdown" ? "stopped" : actual.state === "Booting" ? "booting" : undefined;
      if (status && status !== device.status) {
        this.pool.update({ ...device, status, updatedAt: nowIso() });
        updated += 1;
      }
    }
    this.persist();
    this.pump();
    return { removed, updated };
  }

  /** Captures the current screen of a booted device as PNG. */
  async screenshot(id: string): Promise<Buffer> {
    const device = this.requireDevice(id);
    if (device.status !== "ready" && device.status !== "busy") {
      throw new DomainError(`"${device.name}" is ${device.status}; only a running device has a screen.`, 409);
    }
    if (device.type === "vm") return Buffer.from(this.vmEngine.getScreenshot(id).image, "base64");
    if (!device.simulatorUdid) throw new DomainError(`"${device.name}" has no simulator.`, 409);

    const dir = path.join(this.config.dataDir, "tmp");
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, `${device.id}-${randomUUID()}.png`);
    try {
      await this.engine.simctl.screenshot(device.simulatorUdid, file);
      return fs.readFileSync(file);
    } catch (error) {
      throw new DomainError(`Screenshot failed: ${errorMessage(error)}`, 502);
    } finally {
      fs.rmSync(file, { force: true });
    }
  }

  // ------------------------------------------------------------------ jobs

  private createJob(input: EnqueueTestInput & { runId?: string; requiredRuntime?: string; requiredModelId?: string }): TestJob {
    const now = nowIso();
    const job: TestJob = {
      id: randomUUID(),
      runId: input.runId,
      testTarget: input.testTarget,
      projectPath: input.projectPath,
      workspacePath: input.workspacePath,
      workingDirectory: input.workingDirectory,
      configuration: input.configuration,
      onlyTesting: input.onlyTesting?.length ? input.onlyTesting : undefined,
      status: "queued",
      retries: 0,
      maxRetries: input.maxRetries ?? 0,
      attempts: 0,
      requiredRuntime: input.requiredRuntime,
      requiredModelId: input.requiredModelId,
      autoProvision: input.autoProvision ?? true,
      createdAt: now,
      updatedAt: now
    };
    this.jobs.set(job.id, job);
    this.persist();
    this.hub.emit({ source: "scheduler", type: "log", action: "enqueue_job", message: `Queued ${job.testTarget}`, jobId: job.id, runId: job.runId });
    return job;
  }

  /**
   * Resolves an optional runtime and device type into a combination the host can actually create: a new
   * iPhone does not exist on an old iOS, so an unspecified runtime is chosen to fit the device type and
   * an impossible pairing is a clear 400 instead of a failing `simctl create`.
   */
  private async resolveTarget(runtimeInput?: string, modelInput?: string): Promise<{ runtime: CatalogRuntime; model: CatalogDeviceType }> {
    const catalog = this.engine.catalog;
    const anyModel = modelInput ? await catalog.resolveDeviceType(modelInput) : undefined;
    const runtime = await catalog.resolveRuntime(runtimeInput, anyModel);
    const model = await catalog.resolveDeviceType(modelInput, runtime);
    return { runtime, model };
  }

  async enqueueTest(input: EnqueueTestInput): Promise<TestJob> {
    let requiredRuntime: string | undefined;
    let requiredModelId: string | undefined;
    if (input.requiredRuntime && input.requiredModelId) {
      const { runtime, model } = await this.resolveTarget(input.requiredRuntime, input.requiredModelId);
      requiredRuntime = runtime.identifier;
      requiredModelId = model.identifier;
    } else if (input.requiredRuntime) {
      requiredRuntime = (await this.engine.catalog.resolveRuntime(input.requiredRuntime)).identifier;
    } else if (input.requiredModelId) {
      requiredModelId = (await this.engine.catalog.resolveDeviceType(input.requiredModelId)).identifier;
    }
    const job = this.createJob({ ...input, requiredRuntime, requiredModelId });
    this.pump();
    return this.jobs.get(job.id) ?? job;
  }

  /**
   * Creates one job per runtime x device-type combination, sharing a run. Combinations that cannot exist
   * (a device type the runtime does not support) are skipped and reported, not silently dropped.
   */
  async createRun(input: CreateRunInput): Promise<{ run: TestRunView; jobs: TestJob[]; skipped: Array<{ runtime: string; model: string; reason: string }> }> {
    const runtimes = input.runtimes?.length ? await Promise.all(input.runtimes.map((r) => this.engine.catalog.resolveRuntime(r))) : [undefined];
    const models = input.models?.length ? await Promise.all(input.models.map((m) => this.engine.catalog.resolveDeviceType(m))) : [undefined];
    const uniqueRuntimes = [...new Map(runtimes.map((r) => [r?.identifier ?? "", r])).values()];
    const uniqueModels = [...new Map(models.map((m) => [m?.identifier ?? "", m])).values()];

    const combos: Array<{ runtime?: CatalogRuntime; model?: CatalogDeviceType }> = [];
    const skipped: Array<{ runtime: string; model: string; reason: string }> = [];
    for (const runtime of uniqueRuntimes) {
      for (const model of uniqueModels) {
        if (runtime && model && !CatalogService.supports(runtime, model)) {
          skipped.push({ runtime: runtime.name, model: model.name, reason: `${model.name} is not available on ${runtime.name}` });
        } else {
          combos.push({ runtime, model });
        }
      }
    }

    if (combos.length === 0) {
      throw new DomainError(`None of the requested combinations exist: ${skipped.map((s) => s.reason).join("; ")}`, 400);
    }
    if (combos.length > MAX_MATRIX_JOBS) {
      throw new DomainError(`That matrix would create ${combos.length} jobs; the limit is ${MAX_MATRIX_JOBS}.`, 400);
    }

    const run: TestRun = { id: randomUUID(), name: input.name, scheme: input.testTarget, jobIds: [], maxParallel: input.maxParallel, createdAt: nowIso() };
    this.runs.set(run.id, run);

    const jobs: TestJob[] = [];
    for (const { runtime, model } of combos) {
      const job = this.createJob({ ...input, runId: run.id, requiredRuntime: runtime?.identifier, requiredModelId: model?.identifier });
      run.jobIds.push(job.id);
      jobs.push(job);
    }
    const note = skipped.length ? `, ${skipped.length} unavailable combination${skipped.length === 1 ? "" : "s"} skipped` : "";
    this.hub.emit({ source: "scheduler", type: "started", action: "create_run", message: `Run of ${run.scheme}: ${jobs.length} job${jobs.length === 1 ? "" : "s"}${note}`, runId: run.id });
    this.persist();
    this.pump();
    return { run: this.getRun(run.id)!, jobs: jobs.map((j) => this.jobs.get(j.id) ?? j), skipped };
  }

  listJobs(filter: JobFilter = {}): TestJob[] {
    let items = [...this.jobs.values()].sort((a, b) => b.createdAt.localeCompare(a.createdAt));
    if (filter.status) items = items.filter((j) => j.status === filter.status);
    if (filter.runId) items = items.filter((j) => j.runId === filter.runId);
    return filter.limit ? items.slice(0, filter.limit) : items;
  }

  getJob(id: string): TestJob | undefined {
    return this.jobs.get(id);
  }

  private requireJob(id: string): TestJob {
    const job = this.jobs.get(id);
    if (!job) throw new DomainError(`Job not found: ${id}`, 404);
    return job;
  }

  waitForJob(id: string, timeoutMs: number): Promise<TestJob> {
    const job = this.requireJob(id);
    if (isTerminalJobStatus(job.status)) return Promise.resolve(job);
    return new Promise((resolve) => {
      const list = this.waiters.get(id) ?? [];
      const timer = setTimeout(() => resolve(this.jobs.get(id) ?? job), timeoutMs);
      list.push((finished) => {
        clearTimeout(timer);
        resolve(finished);
      });
      this.waiters.set(id, list);
    });
  }

  queueDepth(): number {
    return [...this.jobs.values()].filter((j) => j.status === "queued" || j.status === "retrying").length;
  }

  async cancelJob(id: string): Promise<TestJob> {
    const job = this.requireJob(id);
    if (isTerminalJobStatus(job.status)) throw new DomainError(`Job is already ${job.status}.`, 409);

    if (job.status === "running") {
      const entry = this.running.get(id);
      this.cancelRequested.add(id);
      entry?.controller.abort();
      await Promise.race([entry?.done, new Promise((resolve) => setTimeout(resolve, 10_000))]);
      return this.jobs.get(id) ?? job;
    }

    const timer = this.retryTimers.get(id);
    if (timer) clearTimeout(timer);
    this.retryTimers.delete(id);
    this.finalizeJob(id, "cancelled", { error: "Cancelled before it started." });
    this.pump();
    return this.jobs.get(id)!;
  }

  async cancelRun(id: string): Promise<TestRunView> {
    const run = this.runs.get(id);
    if (!run) throw new DomainError(`Run not found: ${id}`, 404);
    await Promise.all(
      run.jobIds
        .map((jobId) => this.jobs.get(jobId))
        .filter((job): job is TestJob => !!job && !isTerminalJobStatus(job.status))
        .map((job) => this.cancelJob(job.id).catch(() => undefined))
    );
    return this.getRun(id)!;
  }

  /** Starts a fresh job with the same parameters as a finished one. */
  async rerunJob(id: string): Promise<TestJob> {
    const job = this.requireJob(id);
    if (!isTerminalJobStatus(job.status)) throw new DomainError(`Job is still ${job.status}.`, 409);
    const copy = this.createJob({
      testTarget: job.testTarget,
      projectPath: job.projectPath,
      workspacePath: job.workspacePath,
      workingDirectory: job.workingDirectory,
      configuration: job.configuration,
      onlyTesting: job.onlyTesting,
      maxRetries: job.maxRetries,
      requiredRuntime: job.requiredRuntime,
      requiredModelId: job.requiredModelId,
      autoProvision: job.autoProvision
    });
    this.pump();
    return this.jobs.get(copy.id) ?? copy;
  }

  listRuns(): TestRunView[] {
    return [...this.runs.values()].sort((a, b) => b.createdAt.localeCompare(a.createdAt)).map((run) => this.viewRun(run));
  }

  getRun(id: string): TestRunView | undefined {
    const run = this.runs.get(id);
    return run ? this.viewRun(run) : undefined;
  }

  private viewRun(run: TestRun): TestRunView {
    const counts: Record<JobStatus, number> = { queued: 0, running: 0, retrying: 0, completed: 0, failed: 0, cancelled: 0 };
    let finishedAt: string | undefined;
    for (const jobId of run.jobIds) {
      const job = this.jobs.get(jobId);
      if (!job) continue;
      counts[job.status] += 1;
      if (job.finishedAt && (!finishedAt || job.finishedAt > finishedAt)) finishedAt = job.finishedAt;
    }
    const total = Object.values(counts).reduce((a, b) => a + b, 0);
    const open = counts.queued + counts.running + counts.retrying;
    let status: RunStatus;
    if (open > 0) status = counts.running > 0 || counts.completed + counts.failed + counts.cancelled > 0 ? "running" : "queued";
    else if (counts.failed > 0) status = "failed";
    else if (total > 0 && counts.cancelled === total) status = "cancelled";
    else if (counts.cancelled > 0) status = "cancelled";
    else status = "passed";
    return { ...run, status, counts, finishedAt: open > 0 ? undefined : finishedAt };
  }

  // ------------------------------------------------------------------ dispatcher

  private patchJob(id: string, patch: Partial<TestJob>): TestJob {
    const current = this.requireJob(id);
    const next: TestJob = { ...current, ...patch, updatedAt: nowIso() };
    this.jobs.set(id, next);
    this.persist();
    return next;
  }

  private setWaiting(job: TestJob, reason: string): void {
    if (job.waitingReason === reason) return;
    this.patchJob(job.id, { waitingReason: reason });
  }

  /**
   * Matches queued jobs to devices. It is synchronous and idempotent: call it whenever something that
   * could unblock work happens (job queued, device ready, job finished, capacity freed).
   */
  private pump(): void {
    if (this.closed) return;

    const devices = this.pool.list();
    const capacity = getCapacitySnapshot(devices, this.config.maxLoad);
    let load = capacity.load;
    const claimed = new Set<string>();

    const runningPerRun = new Map<string, number>();
    for (const job of this.jobs.values()) {
      if (job.status === "running" && job.runId) runningPerRun.set(job.runId, (runningPerRun.get(job.runId) ?? 0) + 1);
    }

    const queued = [...this.jobs.values()].filter((j) => j.status === "queued").sort((a, b) => a.createdAt.localeCompare(b.createdAt));

    for (const job of queued) {
      const run = job.runId ? this.runs.get(job.runId) : undefined;
      if (run?.maxParallel && (runningPerRun.get(run.id) ?? 0) >= run.maxParallel) {
        this.setWaiting(job, `This run allows ${run.maxParallel} job${run.maxParallel === 1 ? "" : "s"} at a time`);
        continue;
      }

      const candidates = devices.filter(
        (d) => isTestCapable(d) && !claimed.has(d.id) && (d.status === "ready" || d.status === "booting") && deviceMatchesJob(job, d)
      );
      const ready = candidates.find((d) => d.status === "ready");
      if (ready) {
        claimed.add(ready.id);
        if (run) runningPerRun.set(run.id, (runningPerRun.get(run.id) ?? 0) + 1);
        this.startJob(job.id, ready.id);
        continue;
      }

      const booting = candidates.find((d) => d.status === "booting");
      if (booting) {
        claimed.add(booting.id);
        this.setWaiting(job, "Waiting for a simulator to finish booting");
        continue;
      }

      if (!job.autoProvision) {
        this.setWaiting(job, `No ready simulator matches ${describeRequirements(job)}. Start one, or allow auto-provisioning.`);
        continue;
      }

      const cost = COST_WEIGHTS.simulator;
      if (load + cost > capacity.maxLoad) {
        const idle = devices.find((d) => d.ephemeral && d.status === "ready" && !claimed.has(d.id) && !this.queuedJobWouldUse(d));
        if (idle) void this.retireDevice(idle).catch((error) => this.reportDeviceError(idle.id, error));
        this.setWaiting(job, `Waiting for capacity (${load}/${capacity.maxLoad} in use)`);
        continue;
      }

      void this.provisionFor(job);
      load += cost;
      this.setWaiting(job, "Creating a simulator for this job");
    }

    // Auto-created simulators disappear once nothing queued can use them.
    for (const device of this.pool.list()) {
      if (device.ephemeral && device.status === "ready" && !claimed.has(device.id) && !this.queuedJobWouldUse(device)) {
        void this.retireDevice(device).catch((error) => this.reportDeviceError(device.id, error));
      }
    }
  }

  private queuedJobWouldUse(device: Device): boolean {
    return [...this.jobs.values()].some((j) => (j.status === "queued" || j.status === "retrying") && deviceMatchesJob(j, device));
  }

  private async provisionFor(job: TestJob): Promise<void> {
    try {
      const { runtime, model } = await this.resolveTarget(job.requiredRuntime, job.requiredModelId);
      const device = this.createSimulator({ name: `${model.name} (${runtime.name})`, runtime, model, ephemeral: true });
      this.provisionedFor.set(device.id, job.id);
    } catch (error) {
      if (this.jobs.get(job.id)?.status === "queued") {
        this.finalizeJob(job.id, "failed", { error: errorMessage(error) });
        this.pump();
      }
    }
  }

  private startJob(jobId: string, deviceId: string): void {
    const job = this.requireJob(jobId);
    const device = this.requireDevice(deviceId);
    transitionJobState(job.status, "running");
    transitionDeviceState(device.status, "busy");
    this.provisionedFor.delete(deviceId);

    this.patchDevice(deviceId, { status: "busy", currentJobId: jobId });
    const started = this.patchJob(jobId, {
      status: "running",
      attempts: job.attempts + 1,
      assignedDeviceId: deviceId,
      assignedDeviceName: device.name,
      waitingReason: undefined,
      startedAt: nowIso(),
      finishedAt: undefined,
      error: undefined
    });
    this.metrics?.attemptStarted();
    this.hub.emit({
      source: "scheduler",
      type: "started",
      action: "run_job",
      message: `Running ${started.testTarget} on ${device.name}${started.attempts > 1 ? ` (attempt ${started.attempts})` : ""}`,
      jobId,
      deviceId,
      runId: job.runId
    });

    const controller = new AbortController();
    const done = this.execute(jobId, deviceId, controller.signal)
      .catch((error) => {
        // Only reachable through a bug in the orchestrator itself; never leave a job hanging in "running".
        this.releaseDevice(deviceId);
        if (this.jobs.get(jobId)?.status === "running") {
          this.finalizeJob(jobId, "failed", { error: `Internal error: ${errorMessage(error)}` });
        }
      })
      .finally(() => {
        this.running.delete(jobId);
        this.pump();
      });
    this.running.set(jobId, { controller, done });
  }

  private releaseDevice(deviceId: string): void {
    const device = this.pool.get(deviceId);
    if (!device) return;
    this.patchDevice(deviceId, { status: device.status === "busy" ? "ready" : device.status, currentJobId: undefined });
  }

  private async execute(jobId: string, deviceId: string, signal: AbortSignal): Promise<void> {
    const job = this.requireJob(jobId);
    const device = this.requireDevice(deviceId);
    const attempt = job.attempts;
    const dir = this.artifacts.dirFor(jobId);
    const logFile = path.join(dir, `attempt-${attempt}.log`);
    const bundle = path.join(dir, `attempt-${attempt}.xcresult`);
    const parser = new TestResultParser();

    fs.writeFileSync(logFile, `# MobileLab job ${job.id}, attempt ${attempt}, ${device.name} (${device.simulatorUdid})\n`);

    let result: CommandResult | undefined;
    let launchError: string | undefined;
    try {
      result = await this.engine.xcodebuild.runTests({
        scheme: job.testTarget,
        destination: `platform=iOS Simulator,id=${device.simulatorUdid}`,
        projectPath: job.projectPath,
        workspacePath: job.workspacePath,
        workingDirectory: job.workingDirectory,
        configuration: job.configuration,
        onlyTesting: job.onlyTesting,
        resultBundlePath: bundle,
        logFile,
        timeoutMs: this.config.testTimeoutMs,
        signal,
        onLine: (line) => {
          parser.push(line);
          this.hub.emitTransient({ source: "xcodebuild", type: "log", action: "output", message: line.slice(0, 2000), jobId, deviceId, runId: job.runId });
        }
      });
    } catch (error) {
      launchError = errorMessage(error);
    }

    const parsed = parser.finish();
    this.storeAttemptArtifacts(job, device, attempt, { logFile, bundle, parsed, exitCode: result?.code });
    this.releaseDevice(deviceId);

    const durationMs = result?.durationMs ?? Date.now() - Date.parse(this.requireJob(jobId).startedAt ?? nowIso());
    const summary = parsed.summary;
    const base = { exitCode: result?.code, summary, durationMs };

    if (this.cancelRequested.has(jobId) || result?.aborted) {
      this.cancelRequested.delete(jobId);
      this.finalizeJob(jobId, "cancelled", { ...base, error: this.closed ? "Interrupted: the backend was shut down." : "Cancelled." });
      return;
    }

    if (launchError) {
      this.finalizeJob(jobId, "failed", { ...base, error: launchError });
      return;
    }

    if (result!.code === 0) {
      this.finalizeJob(jobId, "completed", base);
      return;
    }

    const reason = result!.timedOut
      ? `Timed out after ${Math.round(this.config.testTimeoutMs / 1000)}s`
      : summary.buildFailed
        ? summary.errors[0] ?? "The build failed."
        : summary.failed > 0
          ? `${summary.failed} of ${summary.total} test${summary.total === 1 ? "" : "s"} failed`
          : summary.errors[0] ?? `xcodebuild exited with code ${result!.code}`;

    const fresh = this.requireJob(jobId);
    // A compile error or a missing scheme fails the same way every time, so retrying only wastes the queue.
    if (!summary.buildFailed && fresh.retries < fresh.maxRetries && !this.closed) {
      this.scheduleRetry(jobId, { ...base, error: reason });
      return;
    }
    this.finalizeJob(jobId, "failed", { ...base, error: reason });
  }

  private storeAttemptArtifacts(job: TestJob, device: Device, attempt: number, data: { logFile: string; bundle: string; parsed: ParsedTestRun; exitCode?: number }): void {
    const resultsFile = path.join(path.dirname(data.logFile), `attempt-${attempt}.results.json`);
    fs.writeFileSync(resultsFile, JSON.stringify({ jobId: job.id, attempt, device: { id: device.id, name: device.name, runtime: device.runtime }, exitCode: data.exitCode, ...data.parsed }, null, 2));
    this.artifacts.register({ jobId: job.id, type: "log", filePath: data.logFile, attempt });
    this.artifacts.register({ jobId: job.id, type: "results", filePath: resultsFile, attempt });
    this.artifacts.register({ jobId: job.id, type: "xcresult", filePath: data.bundle, attempt });
  }

  private scheduleRetry(jobId: string, patch: Partial<TestJob>): void {
    const job = this.requireJob(jobId);
    transitionJobState(job.status, "retrying");
    const retries = job.retries + 1;
    const delay = backoffMs(retries, this.config.retryBackoffMs);
    this.patchJob(jobId, { ...patch, status: "retrying", retries, waitingReason: `Retrying in ${Math.max(1, Math.round(delay / 1000))}s (${retries}/${job.maxRetries})` });
    this.hub.emit({
      source: "scheduler",
      type: "log",
      action: "retry_backoff",
      message: `${job.testTarget} failed (${patch.error}); retry ${retries}/${job.maxRetries} in ${delay}ms`,
      jobId,
      runId: job.runId
    });
    const timer = setTimeout(() => {
      this.retryTimers.delete(jobId);
      const current = this.jobs.get(jobId);
      if (!current || current.status !== "retrying") return;
      transitionJobState(current.status, "queued");
      this.patchJob(jobId, { status: "queued", waitingReason: undefined });
      this.pump();
    }, delay);
    this.retryTimers.set(jobId, timer);
  }

  private finalizeJob(jobId: string, status: "completed" | "failed" | "cancelled", patch: Partial<TestJob>): void {
    const job = this.requireJob(jobId);
    transitionJobState(job.status, status);
    const finished = this.patchJob(jobId, {
      ...patch,
      status,
      waitingReason: undefined,
      finishedAt: nowIso(),
      error: status === "completed" ? undefined : patch.error
    });

    const device = finished.assignedDeviceId ? this.pool.get(finished.assignedDeviceId) : undefined;
    const label = status === "completed" ? "passed" : status;
    this.hub.emit({
      source: "scheduler",
      type: status === "failed" ? "error" : "finished",
      action: "job_finished",
      message: `${finished.testTarget} ${label}${finished.error && status !== "completed" ? `: ${finished.error}` : ""}`,
      jobId,
      deviceId: finished.assignedDeviceId,
      runId: finished.runId
    });
    this.metrics?.jobFinished(status, finished.durationMs);
    if (this.webhook) void this.webhook.notifyJobFinished(finished, device);

    for (const resolve of this.waiters.get(jobId) ?? []) resolve(finished);
    this.waiters.delete(jobId);
    this.provisionFailures.delete(jobId);
  }

  // ------------------------------------------------------------------ job output

  /** The tail of the newest attempt's raw xcodebuild output. Works while the job is still running. */
  readJobLog(jobId: string, tailBytes = 256 * 1024): { text: string; truncated: boolean; sizeBytes: number; attempt: number } {
    const job = this.requireJob(jobId);
    if (job.attempts === 0) return { text: "", truncated: false, sizeBytes: 0, attempt: 0 };
    const file = path.join(this.artifacts.dirFor(jobId), `attempt-${job.attempts}.log`);
    if (!fs.existsSync(file)) return { text: "", truncated: false, sizeBytes: 0, attempt: job.attempts };

    const size = fs.statSync(file).size;
    const start = Math.max(0, size - tailBytes);
    const fd = fs.openSync(file, "r");
    try {
      const buffer = Buffer.alloc(size - start);
      fs.readSync(fd, buffer, 0, buffer.length, start);
      return { text: buffer.toString("utf-8"), truncated: start > 0, sizeBytes: size, attempt: job.attempts };
    } finally {
      fs.closeSync(fd);
    }
  }

  getJobResults(jobId: string): (ParsedTestRun & { attempt: number; exitCode?: number }) | undefined {
    this.requireJob(jobId);
    const record = this.artifacts.latest(jobId, "results");
    if (!record) return undefined;
    return JSON.parse(fs.readFileSync(record.path, "utf-8")) as ParsedTestRun & { attempt: number; exitCode?: number };
  }

  getJobJUnit(jobId: string): string {
    const job = this.requireJob(jobId);
    const results = this.getJobResults(jobId);
    if (!results) throw new DomainError("This job has no test results yet.", 404);
    return toJUnitXml(job.testTarget, results, job.finishedAt ?? job.updatedAt);
  }

  listArtifacts(jobId: string): ArtifactRecord[] {
    this.requireJob(jobId);
    return this.artifacts.listForJob(jobId);
  }

  // ------------------------------------------------------------------ maintenance

  /** Deletes finished jobs (and their logs and result bundles) older than `days`. */
  cleanup(days: number): { jobsRemoved: number; artifactsRemoved: number; bytesFreed: number; runsRemoved: number } {
    const cutoff = Date.now() - days * 86_400_000;
    const stale = [...this.jobs.values()].filter((j) => isTerminalJobStatus(j.status) && Date.parse(j.finishedAt ?? j.updatedAt) < cutoff).map((j) => j.id);
    const { artifacts, bytes } = this.artifacts.removeForJobs(stale);
    for (const id of stale) this.jobs.delete(id);

    let runsRemoved = 0;
    for (const run of [...this.runs.values()]) {
      if (run.jobIds.every((id) => !this.jobs.has(id))) {
        this.runs.delete(run.id);
        runsRemoved += 1;
      }
    }
    if (stale.length) {
      this.persist();
      this.hub.emit({ source: "system", type: "finished", action: "cleanup", message: `Removed ${stale.length} finished job(s) older than ${days} day(s), freeing ${Math.round(bytes / 1024)} KB` });
    }
    return { jobsRemoved: stale.length, artifactsRemoved: artifacts, bytesFreed: bytes, runsRemoved };
  }

  /** CPU share of this backend process since the previous call (100 = one full core). */
  private processCpuPercent(): number {
    const now = Date.now();
    const usage = process.cpuUsage();
    const elapsedMs = now - this.cpuSample.at;
    const usedMs = (usage.user - this.cpuSample.usage.user + (usage.system - this.cpuSample.usage.system)) / 1000;
    if (elapsedMs >= 500) this.cpuSample = { at: now, usage };
    return elapsedMs > 0 ? Math.round(Math.min(100 * os.cpus().length, (usedMs / elapsedMs) * 100) * 10) / 10 : 0;
  }

  stats() {
    const jobs = [...this.jobs.values()];
    const byStatus: Record<string, number> = {};
    for (const job of jobs) byStatus[job.status] = (byStatus[job.status] ?? 0) + 1;
    return {
      devices: this.pool.list().length,
      jobs: jobs.length,
      queueDepth: this.queueDepth(),
      running: jobs.filter((j) => j.status === "running").length,
      jobsByStatus: byStatus,
      capacity: this.capacity(),
      hostMemoryFreeGb: Math.round((os.freemem() / 1024 ** 3) * 10) / 10,
      artifactBytes: this.artifacts.totalBytes(),
      process: {
        pid: process.pid,
        uptimeSeconds: Math.round(process.uptime()),
        rssBytes: process.memoryUsage().rss,
        cpuPercent: this.processCpuPercent()
      }
    };
  }
}
