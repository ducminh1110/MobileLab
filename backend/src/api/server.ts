import path from "node:path";
import fastifyStatic from "@fastify/static";
import Fastify, { FastifyInstance } from "fastify";
import type { AppServices } from "../app";
import { registerLogSocket } from "../websocket/registerLogSocket";
import { errorHandler } from "./middleware/errorHandler";
import { registerAuth } from "./plugins/auth";
import { registerDeviceRoutes } from "./routes/devices";
import { registerEventRoutes } from "./routes/events";
import { registerMcpRoutes } from "./routes/mcp";
import { registerMetricsRoutes } from "./routes/metrics";
import { registerSystemRoutes } from "./routes/system";
import { registerTestRoutes } from "./routes/tests";
import { registerVmRoutes } from "./routes/vms";

/** Keeps API tokens passed as `?token=` out of the access log. */
function redact(url: string): string {
  return url.replace(/([?&]token=)[^&]*/g, "$1[redacted]");
}

const CSP = "default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data: blob:; style-src 'self'; script-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'";

export function buildServer(services: AppServices): FastifyInstance {
  const app = Fastify({
    logger: {
      level: services.config.logLevel,
      serializers: {
        req: (request) => ({ method: request.method, url: redact(request.url), reqId: request.id })
      }
    }
  });

  app.setErrorHandler(errorHandler);
  app.setNotFoundHandler((request, reply) => {
    reply.code(404).send({ error: "not_found", message: `Route ${request.method} ${redact(request.url.split("?")[0])} not found` });
  });

  app.addHook("onSend", async (_request, reply, payload) => {
    reply.header("x-content-type-options", "nosniff");
    if (String(reply.getHeader("content-type") ?? "").startsWith("text/html")) {
      reply.header("content-security-policy", CSP);
      reply.header("referrer-policy", "no-referrer");
    }
    return payload;
  });

  registerAuth(app, services.config.apiToken);

  registerSystemRoutes(app, services);
  registerDeviceRoutes(app, services);
  registerTestRoutes(app, services);
  registerEventRoutes(app, services);
  registerMetricsRoutes(app, services);
  registerVmRoutes(app, services);
  registerMcpRoutes(app, services);
  registerLogSocket(app, services);

  // The dashboard: plain static files, no build step. Resolves from both src/ and dist/.
  app.register(fastifyStatic, {
    root: path.join(__dirname, "..", "..", "public"),
    cacheControl: false,
    setHeaders: (res) => res.setHeader("cache-control", "no-cache")
  });

  return app;
}
