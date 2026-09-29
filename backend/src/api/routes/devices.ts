import { FastifyInstance } from "fastify";
import type { AppServices } from "../../app";
import { DomainError } from "../../utils/errors";
import { idParams } from "../schemas/common";
import { deviceIdBodySchema, spawnDeviceSchema } from "../schemas/devices";

export function registerDeviceRoutes(app: FastifyInstance, { orchestrator }: AppServices): void {
  app.get("/devices", async () => ({ items: orchestrator.listDevices(), capacity: orchestrator.capacity() }));

  app.get<{ Params: { id: string } }>("/devices/:id", async (request) => {
    const { id } = idParams.parse(request.params);
    const device = orchestrator.getDevice(id);
    if (!device) throw new DomainError(`Device not found: ${id}`, 404);
    return device;
  });

  app.post("/devices/spawn", async (request, reply) => {
    const payload = spawnDeviceSchema.parse(request.body ?? {});
    const device = await orchestrator.spawnDevice(payload);
    if (payload.wait === false) reply.code(202);
    return device;
  });

  // `boot` and `shutdown` ignore any client-supplied target state: the URL is the intent.
  app.post("/devices/boot", async (request) => orchestrator.bootDevice(deviceIdBodySchema.parse(request.body).id));
  app.post("/devices/shutdown", async (request) => orchestrator.shutdownDevice(deviceIdBodySchema.parse(request.body).id));
  app.post<{ Params: { id: string } }>("/devices/:id/boot", async (request) => orchestrator.bootDevice(idParams.parse(request.params).id));
  app.post<{ Params: { id: string } }>("/devices/:id/shutdown", async (request) => orchestrator.shutdownDevice(idParams.parse(request.params).id));

  app.delete<{ Params: { id: string } }>("/devices/:id", async (request, reply) => {
    await orchestrator.deleteDevice(idParams.parse(request.params).id);
    reply.code(204);
  });

  /** Reconcile the pool with what `simctl` reports (after simulators were changed outside MobileLab). */
  app.post("/devices/sync", async () => orchestrator.syncDevices());

  app.get<{ Params: { id: string } }>("/devices/:id/screenshot", async (request, reply) => {
    const png = await orchestrator.screenshot(idParams.parse(request.params).id);
    reply.header("cache-control", "no-store").type("image/png");
    return png;
  });
}
