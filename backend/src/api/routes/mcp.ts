import { FastifyInstance } from "fastify";
import type { AppServices } from "../../app";
import { McpRequest, McpServer } from "../../mcp/mcpServer";

const invalid = { jsonrpc: "2.0", id: null, error: { code: -32600, message: "Invalid Request: expected a JSON-RPC 2.0 request object" } };

export function registerMcpRoutes(app: FastifyInstance, { orchestrator, config }: AppServices): void {
  const mcp = new McpServer(orchestrator, { vmEnabled: config.experimentalVm });

  const isRequest = (value: unknown): value is McpRequest =>
    typeof value === "object" && value !== null && typeof (value as McpRequest).method === "string";

  app.post("/mcp", async (request, reply) => {
    const body = request.body;

    if (Array.isArray(body)) {
      if (body.length === 0 || !body.every(isRequest)) {
        reply.code(400);
        return invalid;
      }
      const responses = (await Promise.all(body.map((item) => mcp.handleRequest(item)))).filter(Boolean);
      if (responses.length === 0) {
        reply.code(202);
        return "";
      }
      return responses;
    }

    if (!isRequest(body)) {
      reply.code(400);
      return invalid;
    }
    const response = await mcp.handleRequest(body);
    if (!response) {
      reply.code(202);
      return "";
    }
    return response;
  });
}
