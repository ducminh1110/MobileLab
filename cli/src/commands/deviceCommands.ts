import { CliContext } from "../context";
import { Device, SpawnRequest } from "../client/types";
import { resolveDevice, resolveDeviceId, withListHint } from "../utils/resolve";
import { withSpinner } from "../utils/progress";
import { oneLine, sanitize, shortId } from "../utils/format";
import { Column, renderTable } from "../utils/table";
import { Output } from "../utils/output";

const DEVICES_HINT = 'Run "ioslab devices" to see the available devices.';

export function deviceColumns(out: Output): Array<Column<Device>> {
  return [
    { header: "ID", value: (d) => shortId(d.id) },
    { header: "NAME", value: (d) => d.name, flex: true },
    { header: "RUNTIME", value: (d) => d.runtimeName ?? d.runtime, flex: true },
    { header: "STATUS", value: (d) => d.status, style: (t) => out.deviceStatus(t) },
    { header: "BACKEND", value: (d) => d.backend },
    { header: "EPHEMERAL", value: (d) => (d.ephemeral ? "yes" : "no") }
  ];
}

export function describeDevice(device: Pick<Device, "name" | "runtimeName" | "runtime" | "type">): string {
  const where = device.type === "vm" ? "" : ` (${oneLine(device.runtimeName ?? device.runtime)})`;
  return `${oneLine(device.name)}${where}`;
}

/** The one-line caveat printed by everything that touches the VM backend. */
export function vmNote(ctx: CliContext): void {
  const note = "Note: the VM backend is experimental and simulated: it does not start a real VM and cannot run tests.";
  // Keep stdout clean for --json consumers.
  if (ctx.config.json) ctx.out.errLine(note);
  else ctx.out.line(ctx.out.c.yellow(note));
}

export async function devicesCommand(ctx: CliContext): Promise<number> {
  const { out } = ctx;
  const listing = await ctx.client.listDevices();
  if (ctx.config.json) {
    out.json(listing);
    return 0;
  }
  if (listing.items.length === 0) {
    out.line('No devices. Create one with "ioslab spawn", or run tests and one is created on demand.');
    return 0;
  }
  out.lines(renderTable(deviceColumns(out), listing.items, { maxWidth: out.tableWidth, headerStyle: out.dim }));
  return 0;
}

export interface SpawnOptions {
  runtime?: string;
  model?: string;
  vm?: boolean;
  /** `--no-wait` sets this to false. */
  wait?: boolean;
}

export async function spawnCommand(ctx: CliContext, name: string | undefined, options: SpawnOptions): Promise<number> {
  const { out } = ctx;
  const body: SpawnRequest = { name, runtime: options.runtime, modelId: options.model, type: options.vm ? "vm" : "simulator", wait: options.wait === false ? false : undefined };

  const label = name ? `"${oneLine(name)}"` : "a simulator";
  const device = await withSpinner(ctx, `Creating ${label} (booting can take a minute)`, () => ctx.client.spawnDevice(body));
  if (options.vm) vmNote(ctx);

  if (ctx.config.json) {
    out.json(device);
  } else if (device.status === "ready") {
    out.line(`${out.c.green("✔")} Created ${describeDevice(device)}  ${out.dim(`id ${shortId(device.id)}`)}  ${out.deviceStatus(device.status)}`);
  } else {
    out.line(`Creating ${describeDevice(device)}  ${out.dim(`id ${shortId(device.id)}`)}  ${out.deviceStatus(device.status)}. Check on it with "ioslab devices".`);
  }
  return 0;
}

async function withDevice<T>(ctx: CliContext, ref: string, run: (id: string) => Promise<T>, options: { type?: Device["type"] } = {}): Promise<T> {
  const id = await resolveDeviceId(ctx.client, ref, options);
  try {
    return await run(id);
  } catch (error) {
    throw withListHint(error, DEVICES_HINT);
  }
}

export async function bootCommand(ctx: CliContext, ref: string): Promise<number> {
  const device = await withSpinner(ctx, `Booting ${sanitize(ref)}`, () => withDevice(ctx, ref, (id) => ctx.client.bootDevice(id)));
  if (ctx.config.json) ctx.out.json(device);
  else ctx.out.line(`${ctx.out.c.green("✔")} ${describeDevice(device)} is ${ctx.out.deviceStatus(device.status)}  ${ctx.out.dim(`id ${shortId(device.id)}`)}`);
  return 0;
}

export async function shutdownCommand(ctx: CliContext, ref: string): Promise<number> {
  const device = await withSpinner(ctx, `Shutting down ${sanitize(ref)}`, () => withDevice(ctx, ref, (id) => ctx.client.shutdownDevice(id)));
  if (ctx.config.json) ctx.out.json(device);
  else ctx.out.line(`${ctx.out.c.green("✔")} ${describeDevice(device)} is ${ctx.out.deviceStatus(device.status)}  ${ctx.out.dim(`id ${shortId(device.id)}`)}`);
  return 0;
}

export async function removeCommand(ctx: CliContext, ref: string): Promise<number> {
  const { out } = ctx;
  // Resolving also tells us the device's name for the confirmation (DELETE answers 204 with no body).
  const { id, device } = await resolveDevice(ctx.client, ref);
  try {
    await withSpinner(ctx, `Removing ${sanitize(ref)}`, () => ctx.client.deleteDevice(id));
  } catch (error) {
    throw withListHint(error, DEVICES_HINT);
  }
  if (ctx.config.json) out.json({ removed: id });
  else out.line(`${out.c.green("✔")} Removed ${device ? describeDevice(device) : sanitize(ref)}  ${out.dim(`id ${shortId(id)}`)}`);
  return 0;
}
