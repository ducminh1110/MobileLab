import { Counter, Gauge, Histogram, Registry, collectDefaultMetrics } from "prom-client";
import type { CapacitySnapshot } from "../scheduler/policies/capacityPolicy";
import type { Device, JobStatus, TestJob } from "../simulator/models/types";

export interface MetricsSource {
  listDevices(): Device[];
  listJobs(): TestJob[];
  capacity(): CapacitySnapshot;
}

/**
 * Prometheus metrics for one backend instance. Gauges are read from live state at scrape time, so they
 * can never drift from what the API reports. Metric names keep the historical `ioslab_` prefix so
 * existing dashboards keep working.
 */
export class Metrics {
  readonly registry = new Registry();
  private source?: MetricsSource;
  private readonly executed: Counter;
  private readonly finished: Counter<"status">;
  private readonly duration: Histogram;

  constructor(options: { defaultMetrics?: boolean } = {}) {
    if (options.defaultMetrics) collectDefaultMetrics({ register: this.registry });
    const registers = [this.registry];
    const self = this;

    new Gauge({
      name: "ioslab_active_devices",
      help: "Number of booted devices (ready, busy or booting)",
      registers,
      collect() {
        const devices = self.source?.listDevices() ?? [];
        this.set(devices.filter((d) => ["ready", "busy", "booting"].includes(d.status)).length);
      }
    });

    new Gauge({
      name: "ioslab_devices",
      help: "Devices in the pool by status",
      labelNames: ["status"],
      registers,
      collect() {
        this.reset();
        for (const device of self.source?.listDevices() ?? []) this.inc({ status: device.status });
      }
    });

    new Gauge({
      name: "ioslab_queued_jobs",
      help: "Number of jobs waiting for a device",
      registers,
      collect() {
        this.set((self.source?.listJobs() ?? []).filter((j) => j.status === "queued" || j.status === "retrying").length);
      }
    });

    new Gauge({
      name: "ioslab_running_jobs",
      help: "Number of jobs currently executing",
      registers,
      collect() {
        this.set((self.source?.listJobs() ?? []).filter((j) => j.status === "running").length);
      }
    });

    new Gauge({
      name: "ioslab_capacity_load",
      help: "Cost of the devices currently booted",
      registers,
      collect() {
        this.set(self.source?.capacity().load ?? 0);
      }
    });

    new Gauge({
      name: "ioslab_capacity_max",
      help: "Maximum cost this host will carry",
      registers,
      collect() {
        this.set(self.source?.capacity().maxLoad ?? 0);
      }
    });

    this.executed = new Counter({ name: "ioslab_executed_jobs_total", help: "Total job attempts started", registers });
    this.finished = new Counter({ name: "ioslab_jobs_finished_total", help: "Jobs that reached a final state", labelNames: ["status"], registers });
    this.duration = new Histogram({
      name: "ioslab_job_duration_seconds",
      help: "Wall-clock duration of finished jobs",
      buckets: [1, 5, 15, 30, 60, 120, 300, 600, 1800],
      registers
    });
  }

  bind(source: MetricsSource): void {
    this.source = source;
  }

  attemptStarted(): void {
    this.executed.inc();
  }

  jobFinished(status: JobStatus, durationMs?: number): void {
    this.finished.inc({ status });
    if (durationMs !== undefined) this.duration.observe(durationMs / 1000);
  }
}
