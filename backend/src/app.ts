import path from "node:path";
import type { FastifyInstance } from "fastify";
import { buildServer } from "./api/server";
import { AppConfig } from "./config/env";
import { EventHub } from "./core/eventHub";
import { DoctorService } from "./doctor/doctorService";
import { ArtifactService } from "./artifacts/artifactService";
import { Metrics } from "./metrics/prometheus";
import { WebhookNotifier } from "./notify/webhook";
import { OrchestratorService } from "./orchestrator/services/orchestratorService";
import { CommandRunner, RealCommandRunner } from "./simulator/engine/commandRunner";
import { MockCommandRunner } from "./simulator/engine/mockCommandRunner";
import { SimulatorEngine } from "./simulator/engine/simulatorEngine";
import { VMEngine } from "./simulator/engine/vmEngine";
import { StateStore } from "./store/stateStore";

export interface AppServices {
  config: AppConfig;
  hub: EventHub;
  orchestrator: OrchestratorService;
  engine: SimulatorEngine;
  metrics: Metrics;
  doctor: DoctorService;
}

export interface CreateAppOptions {
  /** Replace the command runner (tests). Defaults to the real one, or the mock in demo mode. */
  runner?: CommandRunner;
  /** Keep state in memory only. */
  ephemeral?: boolean;
}

export interface App {
  app: FastifyInstance;
  services: AppServices;
  close(): Promise<void>;
}

export async function createServices(config: AppConfig, options: CreateAppOptions = {}): Promise<AppServices> {
  const hub = new EventHub();
  const runner = options.runner ?? (config.mock ? new MockCommandRunner({ latencyMs: config.mockLatencyMs }) : new RealCommandRunner());
  const engine = new SimulatorEngine(runner, config.workspaceRoot);

  const store = new StateStore(options.ephemeral ? undefined : path.join(config.dataDir, "state.json"));
  const state = store.load();
  if (store.recoveredFrom) {
    hub.emit({ source: "system", type: "error", action: "state_recovered", message: `State file was unreadable and was moved to ${store.recoveredFrom}; starting fresh.` });
  }

  const artifacts = new ArtifactService(path.join(config.dataDir, "artifacts"), state.artifacts);
  const vmEngine = new VMEngine(hub, state.vms, config.mock ? Math.max(20, Math.round(config.mockLatencyMs / 3)) : 0);
  const metrics = new Metrics({ defaultMetrics: config.nodeEnv !== "test" });
  const webhook = config.webhookUrl ? new WebhookNotifier(config.webhookUrl, hub) : undefined;

  const orchestrator = new OrchestratorService({ config, hub, engine, vmEngine, store, artifacts, metrics, webhook }, state);
  await orchestrator.initialize();

  return { config, hub, orchestrator, engine, metrics, doctor: new DoctorService(config, engine) };
}

/** Wires everything together. Used by the entry point and by tests. */
export async function createApp(config: AppConfig, options: CreateAppOptions = {}): Promise<App> {
  const services = await createServices(config, options);
  const app = buildServer(services);
  return {
    app,
    services,
    async close() {
      await services.orchestrator.shutdown();
      await app.close();
    }
  };
}
