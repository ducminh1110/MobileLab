import { CliContext } from "../context";
import { Device } from "../client/types";
import { formatDuration, plural, sanitize } from "../utils/format";
import { renderTable } from "../utils/table";
import { deviceColumns } from "./deviceCommands";
import { jobListColumns } from "./jobFormat";

const RECENT_JOBS = 10;

export function demoBanner(ctx: CliContext): string {
  return ctx.out.c.bold.yellow("!! DEMO MODE: simulator commands are simulated and test results are NOT real !!");
}

/** `ioslab status`: what the backend is, what it can carry, which devices exist and what ran recently. */
export async function statusCommand(ctx: CliContext): Promise<number> {
  const { client, out } = ctx;
  const [health, capabilities, devices, jobs] = await Promise.all([client.health(), client.capabilities(), client.listDevices(), client.listJobs({ limit: RECENT_JOBS })]);

  if (ctx.config.json) {
    out.json({ url: client.baseUrl, health, capabilities, capacity: devices.capacity, devices: devices.items, jobs: jobs.items });
    return 0;
  }

  out.line(`${out.heading("MobileLab backend")} ${sanitize(client.baseUrl)}  v${sanitize(health.version)}  up ${formatDuration(health.uptimeSeconds * 1000)}`);
  if (health.mode === "demo") {
    out.line(demoBanner(ctx));
    if (capabilities.modeReason === "auto" && capabilities.platform) {
      out.line(out.dim(`The backend host is ${sanitize(capabilities.platform)}, not macOS. Run it on a Mac with Xcode to drive real simulators.`));
    }
  } else {
    out.line(out.c.green("Live mode: driving real Xcode simulators."));
  }

  const capacity = devices.capacity;
  const usage = capacity.load >= capacity.maxLoad ? out.c.yellow(`${capacity.load}/${capacity.maxLoad}`) : `${capacity.load}/${capacity.maxLoad}`;
  out.line(`Capacity: ${usage} units in use ${out.dim(`(${plural(capacity.cpuCores, "core")}, ${capacity.memoryGb} GB RAM; a simulator costs 1 unit)`)}`);

  out.line();
  out.line(out.heading(`Devices (${devices.items.length})`));
  if (devices.items.length === 0) {
    out.line(out.dim('  None yet. Create one with: ioslab spawn "iPhone 15" (or just run tests; devices are created on demand).'));
  } else {
    out.lines(renderTable<Device>(deviceColumns(out), devices.items, { maxWidth: out.tableWidth, indent: "  ", headerStyle: out.dim }));
  }

  out.line();
  out.line(out.heading(`Recent jobs (${jobs.items.length})`));
  if (jobs.items.length === 0) {
    out.line(out.dim("  None yet. Run some with: ioslab test run <scheme>"));
  } else {
    out.lines(renderTable(jobListColumns(out), jobs.items, { maxWidth: out.tableWidth, indent: "  ", headerStyle: out.dim }));
  }
  return 0;
}
