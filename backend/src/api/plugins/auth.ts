import { createHash, timingSafeEqual } from "node:crypto";
import { FastifyInstance } from "fastify";

const PUBLIC_EXACT = new Set(["/", "/health", "/favicon.svg", "/index.html"]);

function digest(value: string): Buffer {
  return createHash("sha256").update(value).digest();
}

export function isPublicPath(url: string): boolean {
  const pathname = url.split("?")[0];
  return PUBLIC_EXACT.has(pathname) || pathname.startsWith("/assets/");
}

/** Pulls the token out of `Authorization: Bearer <token>` or `?token=` (needed by EventSource and WebSocket, which cannot set headers). */
export function extractToken(headers: Record<string, unknown>, url: string): string | undefined {
  const header = headers.authorization;
  if (typeof header === "string" && header.toLowerCase().startsWith("bearer ")) return header.slice(7).trim();
  const query = url.split("?")[1];
  return query ? new URLSearchParams(query).get("token") ?? undefined : undefined;
}

export function tokensMatch(expected: string, provided: string | undefined): boolean {
  if (!provided) return false;
  return timingSafeEqual(digest(expected), digest(provided));
}

/** Optional shared-secret protection. The dashboard shell and /health stay open so the page can ask for the token. */
export function registerAuth(app: FastifyInstance, token: string | undefined): void {
  if (!token) return;
  app.addHook("onRequest", async (request, reply) => {
    if (isPublicPath(request.url)) return;
    if (tokensMatch(token, extractToken(request.headers, request.url))) return;
    reply.header("www-authenticate", 'Bearer realm="mobilelab"');
    reply.code(401).send({ error: "unauthorized", message: "Missing or invalid API token. Send it as `Authorization: Bearer <token>`." });
  });
}
