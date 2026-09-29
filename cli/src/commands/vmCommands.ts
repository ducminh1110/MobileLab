import { CliContext } from "../context";
import { UsageError } from "../errors";
import { VmConfiguration } from "../client/types";
import { oneLine, sanitize, shortId } from "../utils/format";
import { withSpinner } from "../utils/progress";
import { renderTable } from "../utils/table";
import { describeDevice, vmNote } from "./deviceCommands";
import { resolveDeviceId, withListHint } from "../utils/resolve";

const VM_HINT = 'Run "ioslab vm list" to see the available VMs.';

export interface VmSizeOptions {
  cpu?: number;
  memory?: number;
  disk?: number;
}

export interface VmNewOptions extends VmSizeOptions {
  runtime?: string;
}

function announce(ctx: CliContext, value: unknown, message: string): void {
  if (ctx.config.json) ctx.out.json(value);
  else ctx.out.line(`${ctx.out.c.green("✔")} ${message}`);
}

async function resolveVm(ctx: CliContext, ref: string): Promise<string> {
  return resolveDeviceId(ctx.client, ref, { type: "vm" });
}

export async function vmNewCommand(ctx: CliContext, name: string, options: VmNewOptions): Promise<number> {
  const device = await withSpinner(ctx, `Creating VM "${sanitize(name)}"`, () =>
    ctx.client.spawnVm({ name, runtime: options.runtime, cpu: options.cpu, memory: options.memory, disk: options.disk })
  );
  vmNote(ctx);
  announce(ctx, device, `Created VM ${describeDevice(device)}  ${ctx.out.dim(`id ${shortId(device.id)}`)}  ${ctx.out.deviceStatus(device.status)}`);
  return 0;
}

export async function vmBootCommand(ctx: CliContext, ref: string): Promise<number> {
  const device = await withSpinner(ctx, `Booting VM ${sanitize(ref)}`, async () => {
    const id = await resolveVm(ctx, ref);
    try {
      return await ctx.client.bootDevice(id);
    } catch (error) {
      throw withListHint(error, VM_HINT);
    }
  });
  vmNote(ctx);
  announce(ctx, device, `VM ${describeDevice(device)} is ${ctx.out.deviceStatus(device.status)}`);
  return 0;
}

export async function vmBackupCommand(ctx: CliContext, ref: string, backupName: string): Promise<number> {
  const id = await resolveVm(ctx, ref);
  const vm = await ctx.client.vmBackup(id, backupName).catch((error) => {
    throw withListHint(error, VM_HINT);
  });
  vmNote(ctx);
  announce(ctx, vm, `Saved backup "${sanitize(backupName)}" of VM ${oneLine(vm.name)}  ${ctx.out.dim(`(${vm.backupList.length} backups)`)}`);
  return 0;
}

export async function vmRestoreCommand(ctx: CliContext, ref: string, backupName: string): Promise<number> {
  const id = await resolveVm(ctx, ref);
  const vm = await ctx.client.vmRestore(id, backupName).catch((error) => {
    throw withListHint(error, VM_HINT);
  });
  vmNote(ctx);
  announce(ctx, vm, `Restored VM ${oneLine(vm.name)} to backup "${sanitize(backupName)}"`);
  return 0;
}

export async function vmSwitchCommand(ctx: CliContext, ref: string, options: VmSizeOptions): Promise<number> {
  if (options.cpu === undefined && options.memory === undefined && options.disk === undefined) {
    throw new UsageError("Nothing to switch: give at least one of --cpu, --memory or --disk.");
  }
  const id = await resolveVm(ctx, ref);
  const vm = await ctx.client.vmSwitch(id, { cpu: options.cpu, memory: options.memory, disk: options.disk }).catch((error) => {
    throw withListHint(error, VM_HINT);
  });
  vmNote(ctx);
  announce(ctx, vm, `VM ${oneLine(vm.name)} now has ${vm.cpu} vCPU, ${vm.memory} GB RAM, ${vm.disk} GB disk`);
  return 0;
}

export async function vmListCommand(ctx: CliContext): Promise<number> {
  const { out } = ctx;
  const listing = await ctx.client.listVms();
  vmNote(ctx);
  if (ctx.config.json) {
    out.json(listing);
    return 0;
  }
  if (listing.items.length === 0) {
    out.line('No VMs. Create one with "ioslab vm new <name>".');
    return 0;
  }
  out.lines(
    renderTable<VmConfiguration>(
      [
        { header: "ID", value: (v) => shortId(v.id) },
        { header: "NAME", value: (v) => v.name, flex: true },
        { header: "STATUS", value: (v) => v.status, style: (t) => out.deviceStatus(t) },
        { header: "CPU", value: (v) => String(v.cpu), align: "right" },
        { header: "MEMORY", value: (v) => `${v.memory} GB`, align: "right" },
        { header: "DISK", value: (v) => `${v.disk} GB`, align: "right" },
        { header: "BACKUPS", value: (v) => v.backupList.join(", "), flex: true }
      ],
      listing.items,
      { maxWidth: out.tableWidth, headerStyle: out.dim }
    )
  );
  return 0;
}
