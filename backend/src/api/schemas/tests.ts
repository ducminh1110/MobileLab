import { z } from "zod";
import { pathArg, safeArg } from "./common";

const jobFields = {
  projectPath: pathArg.optional(),
  workspacePath: pathArg.optional(),
  workingDirectory: pathArg.optional(),
  configuration: safeArg.optional(),
  onlyTesting: z.array(safeArg).max(200).optional(),
  maxRetries: z.number().int().min(0).max(5).optional(),
  autoProvision: z.boolean().optional()
};

const projectXorWorkspace = (value: { projectPath?: string; workspacePath?: string }) => !(value.projectPath && value.workspacePath);
const projectMessage = { message: "Give either projectPath or workspacePath, not both", path: ["workspacePath"] };

export const runTestSchema = z
  .object({
    /** The Xcode scheme. `scheme` is accepted as an alias. */
    testTarget: safeArg.optional(),
    scheme: safeArg.optional(),
    requiredRuntime: safeArg.optional(),
    requiredModelId: safeArg.optional(),
    /** Block until the job finishes (or `timeoutSeconds` passes) instead of returning immediately. */
    wait: z.boolean().optional(),
    timeoutSeconds: z.number().int().min(1).max(3600).optional(),
    ...jobFields
  })
  .refine((v) => !!(v.testTarget ?? v.scheme), { message: "testTarget (the Xcode scheme) is required", path: ["testTarget"] })
  .refine(projectXorWorkspace, projectMessage);

export const createRunSchema = z
  .object({
    testTarget: safeArg.optional(),
    scheme: safeArg.optional(),
    name: z.string().trim().max(100).optional(),
    runtimes: z.array(safeArg).max(16).optional(),
    models: z.array(safeArg).max(16).optional(),
    maxParallel: z.number().int().min(1).max(32).optional(),
    ...jobFields
  })
  .refine((v) => !!(v.testTarget ?? v.scheme), { message: "testTarget (the Xcode scheme) is required", path: ["testTarget"] })
  .refine(projectXorWorkspace, projectMessage);

export const jobListQuerySchema = z.object({
  status: z.enum(["queued", "running", "retrying", "completed", "failed", "cancelled"]).optional(),
  runId: z.string().optional(),
  limit: z.coerce.number().int().min(1).max(500).optional()
});

export const cleanupSchema = z.object({ days: z.number().int().min(0).max(3650).optional() });
