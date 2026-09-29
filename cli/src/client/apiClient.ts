import { ApiError, BackendUnreachableError, CliError, EXIT } from "../errors";
import { sanitize } from "../utils/format";
import {
  Capabilities,
  Catalog,
  CreateRunRequest,
  Device,
  DoctorReport,
  Capacity,
  Health,
  JobOutput,
  JobResults,
  JobStatus,
  RunTestRequest,
  SpawnRequest,
  TestJob,
  TestRunView,
  VmConfiguration
} from "./types";

export interface ApiClientOptions {
  /** Normalized base URL, no trailing slash (see `normalizeApiUrl`). */
  baseUrl: string;
  token?: string;
  /** Applied to ordinary requests. Long operations (booting a simulator) pass their own. */
  timeoutMs?: number;
}

type Query = Record<string, string | number | boolean | undefined>;

/** Booting or deleting a real simulator can take minutes; the server holds the request open meanwhile. */
export const LONG_OPERATION_MS = 10 * 60_000;
const DEFAULT_TIMEOUT_MS = 30_000;

interface RawResponse {
  status: number;
  text: string;
}

/**
 * Thin, typed wrapper over the MobileLab HTTP API. It knows nothing about the terminal: failures become
 * `BackendUnreachableError` (exit 2) or `ApiError` (exit 1, carrying the server's own message).
 */
export class ApiClient {
  readonly baseUrl: string;
  private readonly token?: string;
  private readonly timeoutMs: number;

  constructor(options: ApiClientOptions) {
    this.baseUrl = options.baseUrl;
    this.token = options.token;
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  }

  // ---------------------------------------------------------------- transport

  url(pathname: string, query?: Query): string {
    const search = new URLSearchParams();
    for (const [key, value] of Object.entries(query ?? {})) {
      if (value !== undefined) search.set(key, String(value));
    }
    const qs = search.toString();
    return `${this.baseUrl}${pathname}${qs ? `?${qs}` : ""}`;
  }

  /** Headers for anything talking to the backend, including the WebSocket handshake. */
  authHeaders(): Record<string, string> {
    return this.token ? { authorization: `Bearer ${this.token}` } : {};
  }

  /** `ws://…/ws/events?…` for the same host. */
  eventsUrl(query: Query): string {
    return this.url("/ws/events", query).replace(/^http/, "ws");
  }

  private async send(method: string, pathname: string, options: { query?: Query; body?: unknown; timeoutMs?: number } = {}): Promise<RawResponse> {
    const timeoutMs = options.timeoutMs ?? this.timeoutMs;
    const headers: Record<string, string> = { accept: "application/json", ...this.authHeaders() };
    if (options.body !== undefined) headers["content-type"] = "application/json";

    let response: Response;
    let text: string;
    try {
      response = await fetch(this.url(pathname, options.query), {
        method,
        headers,
        body: options.body === undefined ? undefined : JSON.stringify(options.body),
        signal: AbortSignal.timeout(timeoutMs)
      });
      text = await response.text();
    } catch (error) {
      const timedOut = error instanceof Error && (error.name === "TimeoutError" || error.name === "AbortError");
      if (timedOut) {
        throw new BackendUnreachableError(this.baseUrl, `The MobileLab backend at ${this.baseUrl} did not answer within ${Math.round(timeoutMs / 1000)}s.`);
      }
      throw new BackendUnreachableError(this.baseUrl);
    }

    if (!response.ok) throw toApiError(response.status, response.statusText, text);
    return { status: response.status, text };
  }

  private parse<T>(pathname: string, raw: RawResponse): T {
    if (raw.text === "") return undefined as T;
    try {
      return JSON.parse(raw.text) as T;
    } catch {
      throw new CliError(`Unexpected response from ${this.baseUrl}${pathname}: expected JSON. Is ${this.baseUrl} a MobileLab backend?`, EXIT.USAGE);
    }
  }

  async get<T>(pathname: string, query?: Query, timeoutMs?: number): Promise<T> {
    return this.parse<T>(pathname, await this.send("GET", pathname, { query, timeoutMs }));
  }

  async post<T>(pathname: string, body?: unknown, timeoutMs?: number): Promise<T> {
    return this.parse<T>(pathname, await this.send("POST", pathname, { body: body ?? {}, timeoutMs }));
  }

  async delete(pathname: string, timeoutMs?: number): Promise<void> {
    await this.send("DELETE", pathname, { timeoutMs });
  }

  async getText(pathname: string, query?: Query): Promise<string> {
    return (await this.send("GET", pathname, { query })).text;
  }

  // ---------------------------------------------------------------- system

  health(): Promise<Health> {
    return this.get("/health");
  }

  capabilities(): Promise<Capabilities> {
    return this.get("/capabilities");
  }

  catalog(): Promise<Catalog> {
    return this.get("/catalog");
  }

  doctor(): Promise<DoctorReport> {
    return this.get("/doctor");
  }

  // ---------------------------------------------------------------- devices

  listDevices(): Promise<{ items: Device[]; capacity: Capacity }> {
    return this.get("/devices");
  }

  spawnDevice(body: SpawnRequest): Promise<Device> {
    return this.post("/devices/spawn", body, LONG_OPERATION_MS);
  }

  bootDevice(id: string): Promise<Device> {
    return this.post(`/devices/${encodeURIComponent(id)}/boot`, undefined, LONG_OPERATION_MS);
  }

  shutdownDevice(id: string): Promise<Device> {
    return this.post(`/devices/${encodeURIComponent(id)}/shutdown`, undefined, LONG_OPERATION_MS);
  }

  deleteDevice(id: string): Promise<void> {
    return this.delete(`/devices/${encodeURIComponent(id)}`, LONG_OPERATION_MS);
  }

  // ---------------------------------------------------------------- tests

  listJobs(filter: { status?: JobStatus; limit?: number; runId?: string } = {}): Promise<{ items: TestJob[] }> {
    return this.get("/tests", filter);
  }

  getJob(id: string): Promise<TestJob> {
    return this.get(`/tests/${encodeURIComponent(id)}`);
  }

  runTest(body: RunTestRequest): Promise<{ job: TestJob; scheduled: boolean }> {
    return this.post("/tests/run", body);
  }

  createRun(body: CreateRunRequest): Promise<{ run: TestRunView; jobs: TestJob[] }> {
    return this.post("/runs", body);
  }

  getRun(id: string): Promise<{ run: TestRunView; jobs: TestJob[] }> {
    return this.get(`/runs/${encodeURIComponent(id)}`);
  }

  /** Cancelling a running job waits for it to stop, which the server allows up to 10 seconds. */
  cancelJob(id: string): Promise<TestJob> {
    return this.post(`/tests/${encodeURIComponent(id)}/cancel`, undefined, 30_000);
  }

  cancelRun(id: string): Promise<{ run: TestRunView }> {
    return this.post(`/runs/${encodeURIComponent(id)}/cancel`, undefined, 60_000);
  }

  rerunJob(id: string): Promise<{ job: TestJob }> {
    return this.post(`/tests/${encodeURIComponent(id)}/rerun`);
  }

  jobOutput(id: string, tail?: number): Promise<JobOutput> {
    return this.get(`/tests/${encodeURIComponent(id)}/output`, { tail });
  }

  jobResults(id: string): Promise<JobResults> {
    return this.get(`/tests/${encodeURIComponent(id)}/results`);
  }

  jobJunit(id: string): Promise<string> {
    return this.getText(`/tests/${encodeURIComponent(id)}/junit`);
  }

  // ---------------------------------------------------------------- VMs (experimental, simulated)

  listVms(): Promise<{ items: VmConfiguration[]; simulated: boolean }> {
    return this.get("/vms");
  }

  spawnVm(body: { name: string; runtime?: string; cpu?: number; memory?: number; disk?: number }): Promise<Device> {
    return this.post("/vms/spawn", body, LONG_OPERATION_MS);
  }

  vmBackup(id: string, name: string): Promise<VmConfiguration> {
    return this.post(`/vms/${encodeURIComponent(id)}/backup`, { name });
  }

  vmRestore(id: string, name: string): Promise<VmConfiguration> {
    return this.post(`/vms/${encodeURIComponent(id)}/restore`, { name });
  }

  vmSwitch(id: string, config: { cpu?: number; memory?: number; disk?: number }): Promise<VmConfiguration> {
    return this.post(`/vms/${encodeURIComponent(id)}/switch`, config);
  }
}

/** Turns the backend's `{ error, message }` body into an `ApiError`, with a fallback for proxies that answer in HTML. */
export function toApiError(status: number, statusText: string, body: string): ApiError {
  let message: string | undefined;
  let code: string | undefined;
  try {
    const parsed = JSON.parse(body) as { error?: unknown; message?: unknown };
    // Server text reaches the terminal, so it never keeps its escape sequences.
    if (typeof parsed.message === "string" && parsed.message) message = sanitize(parsed.message);
    if (typeof parsed.error === "string") code = parsed.error;
  } catch {
    /* not JSON */
  }
  message ??= `HTTP ${status}${statusText ? ` ${statusText}` : ""}`;
  if (status === 401 || status === 403) {
    message = `Unauthorized (HTTP ${status}): ${message}\nSet IOSLAB_API_TOKEN or pass --token <token>.`;
  }
  return new ApiError(message, status, code);
}
