import { randomUUID } from "node:crypto";
import fs from "node:fs";
import path from "node:path";

export type ArtifactType = "log" | "results" | "xcresult" | "screenshot";

export interface ArtifactRecord {
  id: string;
  jobId: string;
  type: ArtifactType;
  name: string;
  /** Absolute path on the host that runs the backend. */
  path: string;
  isDirectory: boolean;
  sizeBytes: number;
  /** Which execution attempt produced it (1-based). */
  attempt: number;
  createdAt: string;
}

function sizeOf(target: string): number {
  let stat: fs.Stats;
  try {
    stat = fs.statSync(target);
  } catch {
    return 0;
  }
  if (!stat.isDirectory()) return stat.size;
  let total = 0;
  for (const entry of fs.readdirSync(target)) total += sizeOf(path.join(target, entry));
  return total;
}

/** Owns the on-disk layout `<dataDir>/artifacts/<jobId>/...` and the index of what was produced. */
export class ArtifactService {
  private readonly records = new Map<string, ArtifactRecord>();

  constructor(
    private readonly root: string,
    initial: ArtifactRecord[] = []
  ) {
    for (const record of initial) this.records.set(record.id, record);
  }

  dirFor(jobId: string): string {
    const dir = path.join(this.root, jobId);
    fs.mkdirSync(dir, { recursive: true });
    return dir;
  }

  /** Records a file or directory that already exists on disk. Returns undefined if it is missing. */
  register(input: { jobId: string; type: ArtifactType; filePath: string; attempt: number; name?: string }): ArtifactRecord | undefined {
    if (!fs.existsSync(input.filePath)) return undefined;
    const record: ArtifactRecord = {
      id: randomUUID(),
      jobId: input.jobId,
      type: input.type,
      name: input.name ?? path.basename(input.filePath),
      path: input.filePath,
      isDirectory: fs.statSync(input.filePath).isDirectory(),
      sizeBytes: sizeOf(input.filePath),
      attempt: input.attempt,
      createdAt: new Date().toISOString()
    };
    this.records.set(record.id, record);
    return record;
  }

  get(id: string): ArtifactRecord | undefined {
    return this.records.get(id);
  }

  listForJob(jobId: string): ArtifactRecord[] {
    return [...this.records.values()].filter((r) => r.jobId === jobId).sort((a, b) => a.createdAt.localeCompare(b.createdAt));
  }

  latest(jobId: string, type: ArtifactType): ArtifactRecord | undefined {
    return this.listForJob(jobId)
      .filter((r) => r.type === type)
      .sort((a, b) => b.attempt - a.attempt)[0];
  }

  snapshot(): ArtifactRecord[] {
    return [...this.records.values()];
  }

  /** Deletes everything stored for the given jobs. Returns what was actually removed. */
  removeForJobs(jobIds: string[]): { artifacts: number; bytes: number } {
    let artifacts = 0;
    let bytes = 0;
    const wanted = new Set(jobIds);
    for (const [id, record] of this.records) {
      if (wanted.has(record.jobId)) {
        bytes += sizeOf(record.path);
        this.records.delete(id);
        artifacts += 1;
      }
    }
    for (const jobId of jobIds) {
      fs.rmSync(path.join(this.root, jobId), { recursive: true, force: true });
    }
    return { artifacts, bytes };
  }

  totalBytes(): number {
    return sizeOf(this.root);
  }
}
