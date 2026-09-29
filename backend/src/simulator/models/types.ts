export type DeviceStatus =
  | "created"
  | "booting"
  | "ready"
  | "busy"
  | "shutting_down"
  | "stopped"
  | "error";

export interface Runtime {
  id: string;
  name: string;
  version: string;
  isAvailable: boolean;
}

export interface DeviceModel {
  id: string;
  name: string;
  family: "iPhone" | "iPad";
}

export interface SimulatorDevice {
  udid: string;
  name: string;
  state: string;
  runtime: string;
  isAvailable: boolean;
  deviceTypeIdentifier?: string;
}

export type DeviceType = "simulator" | "vm";

/** `simctl` drives real Xcode simulators; `simulated` never touches a real device. */
export type DeviceBackend = "simctl" | "simulated";

export interface Device {
  id: string;
  name: string;
  /** Runtime identifier, e.g. com.apple.CoreSimulator.SimRuntime.iOS-18-0 */
  runtime: string;
  /** Human readable runtime, e.g. "iOS 18.0" */
  runtimeName?: string;
  /** Device type identifier, e.g. com.apple.CoreSimulator.SimDeviceType.iPhone-15 */
  modelId?: string;
  modelName?: string;
  status: DeviceStatus;
  type: DeviceType;
  backend: DeviceBackend;
  /** Only devices that can execute tests are considered by the scheduler. */
  canRunTests: boolean;
  /** Created automatically for a run; removed again when no queued job needs it. */
  ephemeral: boolean;
  simulatorUdid?: string;
  currentJobId?: string;
  lastError?: string;

  // VM (experimental, simulated) attributes
  variant?: string;
  currentPatchTier?: string;
  backupList?: string[];
  cpu?: number;
  memory?: number;
  disk?: number;
  screen?: string;

  createdAt: string;
  updatedAt: string;
}

export type JobStatus = "queued" | "running" | "retrying" | "completed" | "failed" | "cancelled";

export const TERMINAL_JOB_STATUSES: readonly JobStatus[] = ["completed", "failed", "cancelled"];

export function isTerminalJobStatus(status: JobStatus): boolean {
  return TERMINAL_JOB_STATUSES.includes(status);
}

export interface JobTestSummary {
  total: number;
  passed: number;
  failed: number;
  skipped: number;
  durationSeconds: number;
  buildFailed: boolean;
  errors: string[];
}

export interface TestJob {
  id: string;
  runId?: string;
  /** The Xcode scheme to test. (Named testTarget for backwards compatibility.) */
  testTarget: string;
  projectPath?: string;
  workspacePath?: string;
  /** Directory xcodebuild runs in when no project/workspace is given (Swift packages, single-project dirs). */
  workingDirectory?: string;
  configuration?: string;
  onlyTesting?: string[];

  status: JobStatus;
  /** Retries already used. */
  retries: number;
  maxRetries: number;
  /** Number of times execution has started. */
  attempts: number;

  requiredRuntime?: string;
  requiredModelId?: string;
  /** Create a simulator on demand when no compatible device exists. */
  autoProvision: boolean;

  assignedDeviceId?: string;
  assignedDeviceName?: string;
  /** Why a queued job is not running yet. */
  waitingReason?: string;

  exitCode?: number;
  error?: string;
  summary?: JobTestSummary;

  startedAt?: string;
  finishedAt?: string;
  durationMs?: number;
  createdAt: string;
  updatedAt: string;
}

export type RunStatus = "queued" | "running" | "passed" | "failed" | "cancelled";

export interface TestRun {
  id: string;
  name?: string;
  scheme: string;
  jobIds: string[];
  /** At most this many of the run's jobs execute at the same time. */
  maxParallel?: number;
  createdAt: string;
}

export interface TestRunView extends TestRun {
  status: RunStatus;
  counts: Record<JobStatus, number>;
  finishedAt?: string;
}
