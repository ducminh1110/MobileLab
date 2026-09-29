import { FastifyInstance } from "fastify";
import type { AppServices } from "../../app";
import { readVersion } from "../../config/version";
import { getBackendCapabilities } from "../../platform/hostCapabilities";

export function registerSystemRoutes(app: FastifyInstance, services: AppServices): void {
  const { config, orchestrator, engine, doctor } = services;
  const version = readVersion();

  app.get("/health", async () => ({
    status: "ok",
    timestamp: new Date().toISOString(),
    version,
    mode: config.mock ? "demo" : "live",
    uptimeSeconds: Math.round(process.uptime())
  }));

  app.get("/capabilities", async () => getBackendCapabilities(config, orchestrator.capacity()));

  /** What this host can actually create: real runtime and device-type identifiers. */
  app.get("/catalog", async () => {
    const catalog = await engine.catalog.get();
    return { ...catalog, source: config.mock ? "demo" : "simctl" };
  });

  app.get("/doctor", async () => doctor.run());
}
