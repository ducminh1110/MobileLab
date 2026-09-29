import { FastifyInstance } from "fastify";
import { z } from "zod";
import type { AppServices } from "../../app";
import { DomainError } from "../../utils/errors";
import { idParams } from "../schemas/common";

const spawnVmSchema = z.object({
  name: z.string().trim().min(2).max(100),
  runtime: z.string().min(1).optional(),
  cpu: z.number().int().min(1).max(32).optional(),
  memory: z.number().int().min(1).max(128).optional(),
  disk: z.number().int().min(1).max(1024).optional()
});

const inputVmSchema = z.object({
  type: z.enum(["tap", "swipe", "keypress", "scroll"]),
  x: z.number().optional(),
  y: z.number().optional(),
  key: z.string().max(256).optional(),
  duration: z.number().optional()
});

const nameSchema = z.object({ name: z.string().trim().min(1).max(100) });

const switchVmSchema = z.object({
  cpu: z.number().int().min(1).max(32).optional(),
  memory: z.number().int().min(1).max(128).optional(),
  disk: z.number().int().min(1).max(1024).optional()
});

const chaosSchema = z.object({
  networkProfile: z.enum(["Wi-Fi", "3G", "2G", "No-Network"]).optional(),
  thermalThrottle: z.boolean().optional(),
  systemClockOffset: z.number().optional()
});

const agingSchema = z.object({
  batteryDegraded: z.boolean().optional(),
  diskFullLevel: z.number().min(0).max(100).optional()
});

/**
 * Experimental, simulated VM API. Disabled unless IOSLAB_EXPERIMENTAL_VM is set (or the backend is in
 * demo mode), because it models a VM's lifecycle without starting one.
 */
export function registerVmRoutes(app: FastifyInstance, { orchestrator, config }: AppServices): void {
  app.register(async (scope) => {
    scope.addHook("onRequest", async () => {
      if (!config.experimentalVm) {
        throw new DomainError("The experimental VM backend is disabled. Set IOSLAB_EXPERIMENTAL_VM=1 to enable it (it is simulated and cannot run tests).", 501);
      }
    });

    const vm = orchestrator.vmEngine;
    const idOf = (request: { params: unknown }) => idParams.parse(request.params).id;

    scope.get("/vms", async () => ({ items: vm.list(), simulated: true }));

    scope.post("/vms/spawn", async (request) => orchestrator.spawnDevice({ ...spawnVmSchema.parse(request.body), type: "vm" }));

    scope.get<{ Params: { id: string } }>("/vms/:id/screenshot", async (request) => vm.getScreenshot(idOf(request)));
    scope.post<{ Params: { id: string } }>("/vms/:id/input", async (request) => vm.injectInput(idOf(request), inputVmSchema.parse(request.body)));
    scope.post<{ Params: { id: string } }>("/vms/:id/backup", async (request) => vm.backup(idOf(request), nameSchema.parse(request.body).name));
    scope.post<{ Params: { id: string } }>("/vms/:id/restore", async (request) => vm.restoreBackup(idOf(request), nameSchema.parse(request.body).name));
    scope.post<{ Params: { id: string } }>("/vms/:id/switch", async (request) => vm.switchConfig(idOf(request), switchVmSchema.parse(request.body)));
    scope.post<{ Params: { id: string } }>("/vms/:id/chaos", async (request) => vm.injectChaos(idOf(request), chaosSchema.parse(request.body)));
    scope.post<{ Params: { id: string } }>("/vms/:id/aging", async (request) => vm.simulateAging(idOf(request), agingSchema.parse(request.body)));
  });
}
