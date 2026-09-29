import fs from "node:fs";
import path from "node:path";
import { FastifyInstance } from "fastify";
import { z } from "zod";
import type { AppServices } from "../../app";
import { DomainError } from "../../utils/errors";
import { idParams } from "../schemas/common";
import { createRunSchema, jobListQuerySchema, runTestSchema } from "../schemas/tests";

const CONTENT_TYPES: Record<string, string> = {
  ".log": "text/plain; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".xml": "application/xml; charset=utf-8"
};

export function registerTestRoutes(app: FastifyInstance, { orchestrator, hub }: AppServices): void {
  app.get("/tests", async (request) => ({ items: orchestrator.listJobs(jobListQuerySchema.parse(request.query)) }));

  /**
   * Queues a job and returns immediately (202). Pass `wait: true` to hold the request open until the job
   * finishes, which is what scripts usually want; long suites should poll or stream instead.
   */
  app.post("/tests/run", async (request, reply) => {
    const payload = runTestSchema.parse(request.body);
    const { wait, timeoutSeconds, scheme, testTarget, ...rest } = payload;
    let job = await orchestrator.enqueueTest({ ...rest, testTarget: (testTarget ?? scheme)! });
    if (wait) job = await orchestrator.waitForJob(job.id, (timeoutSeconds ?? 600) * 1000);
    reply.code(wait && ["completed", "failed", "cancelled"].includes(job.status) ? 200 : 202);
    return { job, scheduled: job.status !== "queued" };
  });

  app.get<{ Params: { id: string } }>("/tests/:id", async (request) => {
    const { id } = idParams.parse(request.params);
    const job = orchestrator.getJob(id);
    if (!job) throw new DomainError(`Job not found: ${id}`, 404);
    return job;
  });

  app.post<{ Params: { id: string } }>("/tests/:id/cancel", async (request) => orchestrator.cancelJob(idParams.parse(request.params).id));

  app.post<{ Params: { id: string } }>("/tests/:id/rerun", async (request, reply) => {
    const job = await orchestrator.rerunJob(idParams.parse(request.params).id);
    reply.code(202);
    return { job };
  });

  /** Lifecycle events for one job (kept for compatibility; use /events?jobId= for new code). */
  app.get<{ Params: { id: string } }>("/tests/:id/logs", async (request) => {
    const { id } = idParams.parse(request.params);
    if (!orchestrator.getJob(id)) throw new DomainError(`Job not found: ${id}`, 404);
    return { items: hub.list({ jobId: id }) };
  });

  /** The tail of the raw xcodebuild output. Works while the job runs. */
  app.get<{ Params: { id: string } }>("/tests/:id/output", async (request) => {
    const { id } = idParams.parse(request.params);
    const { tail } = z.object({ tail: z.coerce.number().int().min(1024).max(8 * 1024 * 1024).optional() }).parse(request.query);
    return orchestrator.readJobLog(id, tail);
  });

  app.get<{ Params: { id: string } }>("/tests/:id/results", async (request) => {
    const { id } = idParams.parse(request.params);
    return orchestrator.getJobResults(id) ?? { attempt: 0, cases: [], summary: null };
  });

  app.get<{ Params: { id: string } }>("/tests/:id/junit", async (request, reply) => {
    const xml = orchestrator.getJobJUnit(idParams.parse(request.params).id);
    reply.type("application/xml; charset=utf-8");
    return xml;
  });

  app.get<{ Params: { id: string } }>("/tests/:id/artifacts", async (request) => ({
    items: orchestrator.listArtifacts(idParams.parse(request.params).id).map((artifact) => ({
      ...artifact,
      downloadUrl: artifact.isDirectory ? undefined : `/artifacts/${artifact.id}/download`
    }))
  }));

  app.get<{ Params: { id: string } }>("/artifacts/:id/download", async (request, reply) => {
    const { id } = idParams.parse(request.params);
    const artifact = orchestrator.artifacts.get(id);
    if (!artifact || !fs.existsSync(artifact.path)) throw new DomainError("Artifact not found", 404);
    if (artifact.isDirectory) {
      throw new DomainError(`"${artifact.name}" is a folder (an .xcresult bundle). Open it on the host: ${artifact.path}`, 400);
    }
    reply.type(CONTENT_TYPES[path.extname(artifact.path)] ?? "application/octet-stream");
    reply.header("content-disposition", `inline; filename="${artifact.name.replace(/"/g, "")}"`);
    return fs.createReadStream(artifact.path);
  });

  // ---- runs (matrix of runtimes x device types)

  app.get("/runs", async () => ({ items: orchestrator.listRuns() }));

  app.post("/runs", async (request, reply) => {
    const { scheme, testTarget, ...rest } = createRunSchema.parse(request.body);
    const created = await orchestrator.createRun({ ...rest, testTarget: (testTarget ?? scheme)! });
    reply.code(202);
    return created;
  });

  app.get<{ Params: { id: string } }>("/runs/:id", async (request) => {
    const { id } = idParams.parse(request.params);
    const run = orchestrator.getRun(id);
    if (!run) throw new DomainError(`Run not found: ${id}`, 404);
    return { run, jobs: run.jobIds.map((jobId) => orchestrator.getJob(jobId)).filter(Boolean) };
  });

  app.post<{ Params: { id: string } }>("/runs/:id/cancel", async (request) => ({ run: await orchestrator.cancelRun(idParams.parse(request.params).id) }));
}
