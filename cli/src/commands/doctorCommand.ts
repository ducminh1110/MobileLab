import { CliContext } from "../context";
import { CheckStatus, DoctorCheck } from "../client/types";
import { plural, sanitize } from "../utils/format";

/**
 * `ioslab doctor`: shows what the backend's own checks found. Nothing here is hard-coded; every check,
 * message and remedy comes from GET /doctor. Exit 1 only when a check failed.
 */
export async function doctorCommand(ctx: CliContext): Promise<number> {
  const { out } = ctx;
  const report = await ctx.client.doctor();
  const failed = report.status === "unhealthy" || report.checks.some((check) => check.status === "fail");

  if (ctx.config.json) {
    out.json(report);
    return failed ? 1 : 0;
  }

  const mark = (status: CheckStatus): string => {
    switch (status) {
      case "ok":
        return out.c.green("✔ ok  ");
      case "warn":
        return out.c.yellow("! warn");
      case "fail":
        return out.c.red("✖ fail");
      default:
        return out.c.dim("– skip");
    }
  };

  out.line(`${out.heading("MobileLab doctor")} ${out.dim(`(backend mode: ${sanitize(report.mode)})`)}`);
  for (const check of report.checks as DoctorCheck[]) {
    out.line(`${mark(check.status)}  ${out.c.bold(sanitize(check.name))}: ${sanitize(check.message)}`);
    if (check.remedy) out.line(`         ${out.dim(`Fix: ${sanitize(check.remedy)}`)}`);
  }

  const count = (status: CheckStatus) => report.checks.filter((c) => c.status === status).length;
  const parts = [
    count("fail") ? plural(count("fail"), "failure") : "",
    count("warn") ? plural(count("warn"), "warning") : "",
    count("skip") ? `${count("skip")} skipped` : ""
  ].filter(Boolean);
  const colorFor = report.status === "healthy" ? out.c.green : report.status === "degraded" ? out.c.yellow : out.c.red;
  out.line();
  out.line(`Overall: ${colorFor(sanitize(report.status))}${parts.length ? ` (${parts.join(", ")})` : ""}`);
  return failed ? 1 : 0;
}
