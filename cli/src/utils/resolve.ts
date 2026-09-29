import { ApiClient } from "../client/apiClient";
import { ApiError, CliError } from "../errors";
import { Device, TestJob } from "../client/types";
import { oneLine, shortId } from "./format";

const FULL_UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const MAX_CANDIDATES = 10;

export type Match<T> = { kind: "match"; item: T } | { kind: "ambiguous"; candidates: T[] } | { kind: "none" };

/**
 * Finds the one item a reference points at. Order: exact id, exact name, unique id prefix. Names are not
 * prefix-matched: "iPhone" should never quietly pick one of several iPhones.
 */
export function matchReference<T>(items: T[], ref: string, idOf: (item: T) => string, nameOf?: (item: T) => string | undefined): Match<T> {
  const wanted = ref.trim();
  if (!wanted) return { kind: "none" };
  const lower = wanted.toLowerCase();

  const decide = (found: T[]): Match<T> | undefined => {
    if (found.length === 1) return { kind: "match", item: found[0] };
    if (found.length > 1) return { kind: "ambiguous", candidates: found };
    return undefined;
  };

  return (
    decide(items.filter((item) => idOf(item).toLowerCase() === lower)) ??
    (nameOf ? decide(items.filter((item) => nameOf(item) === wanted)) : undefined) ??
    (nameOf ? decide(items.filter((item) => nameOf(item)?.toLowerCase() === lower)) : undefined) ??
    decide(items.filter((item) => idOf(item).toLowerCase().startsWith(lower))) ?? { kind: "none" }
  );
}

function ambiguity<T>(what: string, ref: string, candidates: T[], describe: (item: T) => string): CliError {
  const shown = candidates.slice(0, MAX_CANDIDATES).map((item) => `  ${describe(item)}`);
  if (candidates.length > MAX_CANDIDATES) shown.push(`  …and ${candidates.length - MAX_CANDIDATES} more`);
  return new CliError([`"${oneLine(ref)}" matches ${candidates.length} ${what}s. Use a longer id prefix to pick one:`, ...shown].join("\n"));
}

/** When nothing matched locally the raw reference goes to the server, whose 404 is the authoritative answer. Add a hint to it. */
export function withListHint(error: unknown, hint: string): unknown {
  if (error instanceof ApiError && error.status === 404) return new ApiError(`${error.message}\n${hint}`, error.status, error.code);
  return error;
}

export interface DeviceRef {
  /** Only consider devices of this type (the `vm` commands pass "vm"). */
  type?: Device["type"];
}

/**
 * Device id, unique id prefix, or exact name -> full device id (plus the device, when it was looked up).
 * An unknown reference is passed through unchanged so the server's 404 is the answer the user sees.
 */
export async function resolveDevice(client: ApiClient, ref: string, options: DeviceRef = {}): Promise<{ id: string; device?: Device }> {
  if (FULL_UUID.test(ref)) return { id: ref };
  const { items } = await client.listDevices();
  const pool = options.type ? items.filter((device) => device.type === options.type) : items;
  const result = matchReference(pool, ref, (d) => d.id, (d) => d.name);
  if (result.kind === "match") return { id: result.item.id, device: result.item };
  if (result.kind === "ambiguous") {
    throw ambiguity("device", ref, result.candidates, (d) => `${shortId(d.id)}  ${oneLine(d.name)}  ${d.runtimeName ?? d.runtime}  ${d.status}`);
  }
  return { id: ref };
}

export async function resolveDeviceId(client: ApiClient, ref: string, options: DeviceRef = {}): Promise<string> {
  return (await resolveDevice(client, ref, options)).id;
}

/** Job id or unique id prefix -> full job id. An unknown reference is passed through unchanged. */
export async function resolveJobId(client: ApiClient, ref: string): Promise<string> {
  if (FULL_UUID.test(ref)) return ref;
  const { items } = await client.listJobs({ limit: 500 });
  const result = matchReference<TestJob>(items, ref, (j) => j.id);
  if (result.kind === "match") return result.item.id;
  if (result.kind === "ambiguous") {
    throw ambiguity("job", ref, result.candidates, (j) => `${shortId(j.id)}  ${oneLine(j.testTarget)}  ${j.status}`);
  }
  return ref;
}
