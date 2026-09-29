import { z } from "zod";

/** A value that ends up as its own argv entry for xcodebuild/simctl must not be readable as a flag. */
export const safeArg = z
  .string()
  .trim()
  .min(1)
  .max(256)
  .refine((value) => !value.startsWith("-"), "must not start with '-'")
  .refine((value) => !/[\0\r\n]/.test(value), "must not contain control characters");

export const pathArg = z
  .string()
  .trim()
  .min(1)
  .max(1024)
  .refine((value) => !/[\0\r\n]/.test(value), "must not contain control characters");

export const idParams = z.object({ id: z.string().min(1).max(128) });
