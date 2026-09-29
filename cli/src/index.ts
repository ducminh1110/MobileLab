#!/usr/bin/env node
import { main } from "./cli";

/**
 * The only place the process exits. Everything else returns an exit code (or throws a CliError that
 * `main` turns into one), which is what makes the CLI testable.
 */

const interrupt = new AbortController();
let signals = 0;
for (const [name, code] of [["SIGINT", 130], ["SIGTERM", 143]] as const) {
  process.on(name, () => {
    signals += 1;
    // A second signal means "now": don't wait for the backend to confirm the cancellation.
    if (signals > 1) process.exit(code);
    interrupt.abort(code);
  });
}

// `ioslab logs job | head` closes the pipe early; that is not an error.
process.stdout.on("error", (error: NodeJS.ErrnoException) => {
  if (error.code === "EPIPE") process.exit(0);
});

function flush(stream: NodeJS.WriteStream): Promise<void> {
  return new Promise((resolve) => stream.write("", () => resolve()));
}

main(process.argv.slice(2), { interrupt: interrupt.signal }).then(
  async (code) => {
    await Promise.all([flush(process.stdout), flush(process.stderr)]);
    process.exit(code);
  },
  (error: unknown) => {
    console.error(error);
    process.exit(1);
  }
);
