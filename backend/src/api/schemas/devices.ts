import { z } from "zod";
import { safeArg } from "./common";

export const spawnDeviceSchema = z.object({
  name: z.string().trim().min(1).max(100).optional(),
  /** Identifier ("com.apple.CoreSimulator.SimRuntime.iOS-18-0") or shorthand ("18.0", "iOS 17.5"). */
  runtime: safeArg.optional(),
  /** Identifier or name of the device type ("iPhone 15"). */
  modelId: safeArg.optional(),
  type: z.enum(["simulator", "vm"]).optional(),
  cpu: z.number().int().min(1).max(32).optional(),
  memory: z.number().int().min(1).max(128).optional(),
  disk: z.number().int().min(1).max(1024).optional(),
  wait: z.boolean().optional()
});

export const deviceIdBodySchema = z.object({ id: z.string().min(1) });
