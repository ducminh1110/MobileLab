import { FastifyInstance } from "fastify";
import type { AppServices } from "../../app";
import { cleanupSchema } from "../schemas/tests";

export function registerMetricsRoutes(app: FastifyInstance, { orchestrator, metrics, config }: AppServices): void {
  app.get("/metrics", async (_request, reply) => {
    reply.header("content-type", metrics.registry.contentType);
    return metrics.registry.metrics();
  });

  app.get("/metrics/summary", async () => orchestrator.stats());

  /** Deletes finished jobs and their logs / result bundles older than `days` (default: IOSLAB_RETENTION_DAYS). */
  app.post("/maintenance/cleanup", async (request) => {
    const { days } = cleanupSchema.parse(request.body ?? {});
    return orchestrator.cleanup(days ?? config.retentionDays);
  });
}
