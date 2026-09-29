/** The parts of the MobileLab API the CLI reads. Kept deliberately loose about fields the CLI never touches. */

export type DeviceStatus = "created" | "booting" | "ready" | "busy" | "shutting_down" | "stopped" | "error";

export interface Device {
  id: string;
  name: string;
  runtime: string;
  runtimeName?: string;
  modelId?: string;
  modelName?: string;
  status: DeviceStatus;
  type: "simulator" | "vm";
  backend: "simctl" | "simulated";
  canRunTests: boolean;
  ephemeral: boolean;
  currentJobId?: string;
  lastError?: string;
  cpu?: number;
  memory?: number;
  disk?: number;
  backupList?: string[];
}

export interface Capacity {
  maxLoad: number;
  load: number;
  cpuCores: number;
  memoryGb: number;
}

export type JobStatus = "queued" | "running" | "retrying" | "completed" | "failed" | "cancelled";

export const JOB_STATUSES: readonly JobStatus[] = ["queued", "running", "retrying", "completed", "failed", "cancelled"];

export function isTerminal(status: JobStatus): boolean {
  return status === "completed" || status === "failed" || status === "cancelled";
}

export interface JobSummary {
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
  testTarget: string;
  projectPath?: string;
  workspacePath?: string;
  workingDirectory?: string;
  configuration?: string;
  onlyTesting?: string[];
  status: JobStatus;
  retries: number;
  maxRetries: number;
  attempts: number;
  requiredRuntime?: string;
  requiredModelId?: string;
  autoProvision: boolean;
  assignedDeviceId?: string;
  assignedDeviceName?: string;
  waitingReason?: string;
  exitCode?: number;
  error?: string;
  summary?: JobSummary;
  startedAt?: string;
  finishedAt?: string;
  durationMs?: number;
  createdAt: string;
  updatedAt: string;
}

export type RunStatus = "queued" | "running" | "passed" | "failed" | "cancelled";

export interface TestRunView {
  id: string;
  name?: string;
  scheme: string;
  jobIds: string[];
  maxParallel?: number;
  status: RunStatus;
  counts: Record<JobStatus, number>;
  createdAt: string;
  finishedAt?: string;
}

export interface TestCase {
  className: string;
  name: string;
  status: "passed" | "failed" | "skipped";
  durationSeconds: number;
  message?: string;
}

export interface JobResults {
  attempt: number;
  cases: TestCase[];
  summary: JobSummary | null;
  exitCode?: number;
}

export interface JobOutput {
  text: string;
  truncated: boolean;
  sizeBytes: number;
  attempt: number;
}

export interface Health {
  status: string;
  timestamp: string;
  version: string;
  mode: "live" | "demo";
  uptimeSeconds: number;
}

export interface Capabilities {
  version: string;
  mode: "live" | "demo";
  modeReason?: string;
  platform?: string;
  architecture?: string;
  vm?: { enabled: boolean; simulated: boolean; canRunTests: boolean };
  auth?: { required: boolean };
  capacity: Capacity;
  [key: string]: unknown;
}

export type CheckStatus = "ok" | "warn" | "fail" | "skip";

export interface DoctorCheck {
  id: string;
  name: string;
  status: CheckStatus;
  message: string;
  remedy?: string;
}

export interface DoctorReport {
  status: "healthy" | "degraded" | "unhealthy";
  mode: "live" | "demo";
  generatedAt: string;
  checks: DoctorCheck[];
}

export interface CatalogRuntime {
  identifier: string;
  name: string;
  version: string;
}

export interface CatalogDeviceType {
  identifier: string;
  name: string;
  family: string;
}

export interface Catalog {
  runtimes: CatalogRuntime[];
  deviceTypes: CatalogDeviceType[];
  source?: string;
}

export interface EngineEvent {
  id: number;
  source: string;
  type: "started" | "log" | "finished" | "error";
  action: string;
  message: string;
  timestamp: string;
  jobId?: string;
  deviceId?: string;
  runId?: string;
}

export interface VmConfiguration {
  id: string;
  name: string;
  runtime: string;
  cpu: number;
  memory: number;
  disk: number;
  status: string;
  backupList: string[];
  [key: string]: unknown;
}

/** Body of POST /devices/spawn. */
export interface SpawnRequest {
  name?: string;
  runtime?: string;
  modelId?: string;
  type?: "simulator" | "vm";
  wait?: boolean;
}

/** Fields shared by POST /tests/run and POST /runs. */
export interface JobRequestFields {
  testTarget: string;
  projectPath?: string;
  workspacePath?: string;
  workingDirectory?: string;
  configuration?: string;
  onlyTesting?: string[];
  maxRetries?: number;
  autoProvision?: boolean;
}

export interface RunTestRequest extends JobRequestFields {
  requiredRuntime?: string;
  requiredModelId?: string;
}

export interface CreateRunRequest extends JobRequestFields {
  runtimes?: string[];
  models?: string[];
  maxParallel?: number;
}
