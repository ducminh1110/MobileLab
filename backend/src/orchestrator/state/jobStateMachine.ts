import { JobStatus } from "../../simulator/models/types";
import { DomainError } from "../../utils/errors";

const transitions: Record<JobStatus, JobStatus[]> = {
  queued: ["running", "cancelled", "failed"],
  running: ["completed", "retrying", "failed", "cancelled"],
  retrying: ["queued", "cancelled", "failed"],
  completed: [],
  failed: [],
  cancelled: []
};

export function canTransitionJob(from: JobStatus, to: JobStatus): boolean {
  return transitions[from]?.includes(to) ?? false;
}

export function transitionJobState(from: JobStatus, to: JobStatus): JobStatus {
  if (!canTransitionJob(from, to)) {
    throw new DomainError(`Invalid job transition: ${from} -> ${to}`, 409);
  }
  return to;
}
