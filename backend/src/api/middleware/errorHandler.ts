import { FastifyError, FastifyReply, FastifyRequest } from "fastify";
import { ZodError } from "zod";
import { DomainError } from "../../utils/errors";

function describeIssues(error: ZodError): string {
  return error.issues
    .map((issue) => {
      const where = issue.path.length ? `${issue.path.join(".")}: ` : "";
      return `${where}${issue.message}`;
    })
    .join("; ");
}

/**
 * One error shape for the whole API: `{ error: <code>, message: <human readable> }`. Bad input is a 4xx
 * (never a 500), and unexpected failures are logged with the request id instead of leaking internals.
 */
export function errorHandler(error: FastifyError | Error, request: FastifyRequest, reply: FastifyReply): void {
  if (error instanceof ZodError) {
    reply.code(400).send({ error: "validation_error", message: `Invalid request: ${describeIssues(error)}`, issues: error.issues });
    return;
  }

  if (error instanceof DomainError) {
    reply.code(error.statusCode).send({ error: error.statusCode === 404 ? "not_found" : "request_failed", message: error.message });
    return;
  }

  const statusCode = (error as FastifyError).statusCode;
  if (statusCode && statusCode >= 400 && statusCode < 500) {
    reply.code(statusCode).send({ error: (error as FastifyError).code ?? "bad_request", message: error.message });
    return;
  }

  request.log.error({ err: error }, "unhandled_error");
  reply.code(500).send({ error: "internal_error", message: "Internal server error", requestId: request.id });
}
