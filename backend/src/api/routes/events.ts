import { FastifyInstance } from "fastify";
import { z } from "zod";
import type { AppServices } from "../../app";
import { EngineEvent, EventFilter, matchesFilter } from "../../core/eventHub";

const truthy = z
  .string()
  .optional()
  .transform((v) => v === "1" || v === "true");

export const eventQuerySchema = z.object({
  jobId: z.string().optional(),
  deviceId: z.string().optional(),
  runId: z.string().optional(),
  sinceId: z.coerce.number().int().min(0).optional(),
  limit: z.coerce.number().int().min(1).max(2000).optional(),
  /** Include raw xcodebuild output lines (chatty). Off by default. */
  output: truthy,
  /** WebSocket only: replay this many recent matching events on connect. */
  replay: z.coerce.number().int().min(0).max(500).optional()
});

export type EventQuery = z.infer<typeof eventQuerySchema>;

export function isOutputEvent(event: EngineEvent): boolean {
  return event.action === "output" && event.source === "xcodebuild";
}

export function toFilter(query: EventQuery): EventFilter {
  return { jobId: query.jobId, deviceId: query.deviceId, runId: query.runId, sinceId: query.sinceId, limit: query.limit };
}

export function registerEventRoutes(app: FastifyInstance, { hub }: AppServices): void {
  /** History of lifecycle events. Raw build output is not kept here; read it from /tests/:id/output. */
  app.get("/events", async (request) => {
    const query = eventQuerySchema.parse(request.query);
    return { items: hub.list(toFilter(query)), lastId: hub.lastId };
  });

  /** Server-Sent Events. Reconnects resume from `Last-Event-ID` automatically. */
  app.get("/events/stream", (request, reply) => {
    const query = eventQuerySchema.parse(request.query);
    const filter = toFilter(query);
    const lastEventId = Number(request.headers["last-event-id"]);
    const sinceId = Number.isFinite(lastEventId) && lastEventId > 0 ? lastEventId : query.sinceId;

    reply.hijack();
    const raw = reply.raw;
    raw.writeHead(200, {
      "content-type": "text/event-stream; charset=utf-8",
      "cache-control": "no-cache, no-transform",
      connection: "keep-alive",
      "x-accel-buffering": "no"
    });
    raw.write("retry: 2000\n\n");

    const send = (event: EngineEvent) => {
      if (!matchesFilter(event, filter) || (!query.output && isOutputEvent(event))) return;
      raw.write(`id: ${event.id}\nevent: engine\ndata: ${JSON.stringify(event)}\n\n`);
    };

    if (sinceId !== undefined) for (const event of hub.list({ ...filter, sinceId })) send(event);
    const unsubscribe = hub.subscribe(send);
    const heartbeat = setInterval(() => raw.write(": keep-alive\n\n"), 20_000);
    heartbeat.unref();

    request.raw.on("close", () => {
      clearInterval(heartbeat);
      unsubscribe();
    });
  });
}
