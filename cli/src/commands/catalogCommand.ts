import { CliContext } from "../context";
import { CatalogDeviceType, CatalogRuntime } from "../client/types";
import { sanitize } from "../utils/format";
import { renderTable } from "../utils/table";

/** `ioslab catalog`: the runtimes and device types this host can create; exactly what --runtime and --model accept. */
export async function catalogCommand(ctx: CliContext): Promise<number> {
  const { out } = ctx;
  const catalog = await ctx.client.catalog();

  if (ctx.config.json) {
    out.json(catalog);
    return 0;
  }

  if (catalog.source === "demo") out.line(out.c.yellow("Demo catalog: these runtimes and devices are simulated."));

  out.line(out.heading(`Runtimes (${catalog.runtimes.length})`));
  if (catalog.runtimes.length === 0) out.line(out.dim("  None installed. Install one in Xcode > Settings > Components."));
  else {
    out.lines(
      renderTable<CatalogRuntime>(
        [
          { header: "NAME", value: (r) => r.name },
          { header: "VERSION", value: (r) => r.version },
          { header: "IDENTIFIER", value: (r) => r.identifier }
        ],
        catalog.runtimes,
        { indent: "  ", headerStyle: out.dim }
      )
    );
  }

  out.line();
  out.line(out.heading(`Device types (${catalog.deviceTypes.length})`));
  if (catalog.deviceTypes.length === 0) out.line(out.dim("  None available."));
  else {
    out.lines(
      renderTable<CatalogDeviceType>(
        [
          { header: "NAME", value: (d) => d.name },
          { header: "FAMILY", value: (d) => d.family },
          { header: "IDENTIFIER", value: (d) => d.identifier }
        ],
        catalog.deviceTypes,
        { indent: "  ", headerStyle: out.dim }
      )
    );
  }

  const example = catalog.runtimes[0];
  const model = catalog.deviceTypes.find((d) => /^iPhone \d+$/.test(d.name)) ?? catalog.deviceTypes[0];
  if (example && model) {
    out.line();
    out.line(out.dim(`Use a name, a version or an identifier, e.g.: ioslab spawn --runtime ${sanitize(example.version)} --model "${sanitize(model.name)}"`));
  }
  return 0;
}
