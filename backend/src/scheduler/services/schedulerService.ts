import { Device, TestJob } from "../../simulator/models/types";

/** Whether a device satisfies the runtime / device-type a job asked for. Unspecified means "any". */
export function deviceMatchesJob(job: Pick<TestJob, "requiredRuntime" | "requiredModelId">, device: Pick<Device, "runtime" | "modelId">): boolean {
  const runtimeMatch = !job.requiredRuntime || device.runtime === job.requiredRuntime;
  const modelMatch = !job.requiredModelId || device.modelId === job.requiredModelId;
  return runtimeMatch && modelMatch;
}

/** Only real, booted-or-booting simulators that can execute tests are candidates for a job. */
export function isTestCapable(device: Pick<Device, "canRunTests" | "type">): boolean {
  return device.canRunTests && device.type === "simulator";
}

/** Exponential backoff between retries: 0.5s, 1s, 2s, ... capped at 30s. */
export function backoffMs(retries: number, baseMs = 500): number {
  return Math.min(30_000, baseMs * 2 ** Math.max(0, retries - 1));
}

export function describeRequirements(job: Pick<TestJob, "requiredRuntime" | "requiredModelId">): string {
  const parts = [job.requiredRuntime?.split(".").pop()?.replace("iOS-", "iOS ").replace(/-/g, "."), job.requiredModelId?.split(".").pop()?.replace(/-/g, " ")].filter(Boolean);
  return parts.length ? parts.join(" / ") : "any device";
}
