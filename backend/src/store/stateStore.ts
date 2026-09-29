import fs from "node:fs";
import path from "node:path";
import { ArtifactRecord } from "../artifacts/artifactService";
import { Device, TestJob, TestRun } from "../simulator/models/types";
import type { VMConfiguration } from "../simulator/engine/vmEngine";

export interface PersistedState {
  version: 1;
  devices: Device[];
  jobs: TestJob[];
  runs: TestRun[];
  artifacts: ArtifactRecord[];
  vms: VMConfiguration[];
}

export function emptyState(): PersistedState {
  return { version: 1, devices: [], jobs: [], runs: [], artifacts: [], vms: [] };
}

/**
 * Keeps orchestrator state across restarts in a single JSON file. Writes are debounced and atomic
 * (temp file + rename), so a crash mid-write can never leave a half-written state file. Pass no file
 * to keep everything in memory (tests).
 */
export class StateStore {
  private timer?: NodeJS.Timeout;
  private pending?: () => PersistedState;
  /** Set when an unreadable state file was set aside on load. */
  recoveredFrom?: string;

  constructor(
    private readonly file?: string,
    private readonly debounceMs = 250
  ) {}

  load(): PersistedState {
    if (!this.file || !fs.existsSync(this.file)) return emptyState();
    try {
      const parsed = JSON.parse(fs.readFileSync(this.file, "utf-8")) as Partial<PersistedState>;
      return { ...emptyState(), ...parsed, version: 1 };
    } catch {
      // Don't lose the evidence and don't refuse to start: set the bad file aside and begin empty.
      const aside = `${this.file}.corrupt-${Date.now()}`;
      try {
        fs.renameSync(this.file, aside);
        this.recoveredFrom = aside;
      } catch {
        /* nothing more we can do */
      }
      return emptyState();
    }
  }

  scheduleSave(snapshot: () => PersistedState): void {
    if (!this.file) return;
    this.pending = snapshot;
    if (this.timer) return;
    this.timer = setTimeout(() => this.flush(), this.debounceMs);
    this.timer.unref();
  }

  flush(): void {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
    const snapshot = this.pending;
    this.pending = undefined;
    if (!this.file || !snapshot) return;

    fs.mkdirSync(path.dirname(this.file), { recursive: true });
    const temp = `${this.file}.${process.pid}.tmp`;
    fs.writeFileSync(temp, JSON.stringify(snapshot(), null, 2));
    fs.renameSync(temp, this.file);
  }
}
