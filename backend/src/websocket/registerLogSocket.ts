import websocket from "@fastify/websocket";
import { FastifyInstance, FastifyRequest } from "fastify";
import type { WebSocket } from "ws";
import type { AppServices } from "../app";
import { eventQuerySchema, isOutputEvent, toFilter } from "../api/routes/events";
import { matchesFilter } from "../core/eventHub";

const OPEN = 1;

/**
 * Live event stream. Query: `jobId`, `deviceId`, `runId` narrow it, `output=1` adds raw build output,
 * `replay=N` first sends the last N matching events. `/ws/logs` is the historical name, `/ws/events` the alias.
 */
export function registerLogSocket(app: FastifyInstance, { hub }: AppServices): void {
  app.register(websocket);

  app.register(async (scope) => {
    const handler = (socket: WebSocket, request: FastifyRequest) => {
      const parsed = eventQuerySchema.safeParse(request.query);
      const query = parsed.success ? parsed.data : eventQuerySchema.parse({});
      const filter = toFilter(query);

      const send = (payload: unknown) => {
        if (socket.readyState === OPEN) socket.send(JSON.stringify(payload));
      };

      if (query.replay) for (const event of hub.list({ ...filter, limit: query.replay })) send(event);

      const unsubscribe = hub.subscribe((event) => {
        if (!matchesFilter(event, filter)) return;
        if (!query.output && isOutputEvent(event)) return;
        send(event);
      });

      // Proxies and idle timeouts drop silent sockets; a ping every 25s keeps them open.
      const ping = setInterval(() => {
        if (socket.readyState === OPEN) socket.ping();
      }, 25_000);
      ping.unref();

      socket.on("close", () => {
        clearInterval(ping);
        unsubscribe();
      });
      socket.on("error", () => {
        clearInterval(ping);
        unsubscribe();
      });
    };

    scope.get("/ws/logs", { websocket: true }, handler);
    scope.get("/ws/events", { websocket: true }, handler);
  });
}
