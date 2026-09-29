import { Command, CommanderError, InvalidArgumentError, Option } from "commander";
import { catalogCommand } from "./commands/catalogCommand";
import { bootCommand, devicesCommand, removeCommand, shutdownCommand, spawnCommand } from "./commands/deviceCommands";
import { doctorCommand } from "./commands/doctorCommand";
import { logsCommand } from "./commands/logsCommand";
import { statusCommand } from "./commands/statusCommand";
import { cancelCommand, junitCommand, ListOptions, listCommand, RerunOptions, rerunCommand, RunOptions, runCommand, showCommand } from "./commands/testCommands";
import { vmBackupCommand, vmBootCommand, vmListCommand, vmNewCommand, VmNewOptions, vmRestoreCommand, VmSizeOptions, vmSwitchCommand } from "./commands/vmCommands";
import { GlobalOptions } from "./config";
import { CliContext, createContext, InterruptState } from "./context";
import { CliError, EXIT } from "./errors";
import { createStyle } from "./utils/style";
import { CliIO, CliIOInput, resolveIO } from "./io";
import { JOB_STATUSES } from "./client/types";

const { version } = require("../package.json") as { version: string };

const collect = (value: string, previous: string[] = []): string[] => [...previous, value];

function integer(label: string, min: number) {
  return (value: string): number => {
    const parsed = Number(value);
    if (!Number.isInteger(parsed) || parsed < min) throw new InvalidArgumentError(`${label} must be a whole number of at least ${min}.`);
    return parsed;
  };
}

function positiveNumber(value: string): number {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) throw new InvalidArgumentError("must be a number of seconds greater than 0.");
  return parsed;
}

const EXAMPLES = `
Examples:
  $ ioslab status                                   what is running, and is the backend real or demo?
  $ ioslab catalog                                  runtimes and device types you can ask for
  $ ioslab spawn "My iPhone" --runtime 18.0         create and boot a simulator
  $ ioslab test run MyAppTests --project MyApp.xcodeproj
  $ ioslab test run MyAppTests --runtime 18.0 --runtime 17.5 --junit build/tests.xml
  $ ioslab logs -f 3f2a9c1e                         follow a job's raw output (any unique id prefix works)

Environment:
  IOSLAB_API_URL     backend address (default http://127.0.0.1:4000)
  IOSLAB_API_TOKEN   bearer token, when the backend is protected with one
  NO_COLOR           turn off colors (they are also off whenever output is not a terminal)

Exit codes:
  0    success
  1    what you asked for failed: tests failed or were cancelled, a device operation failed, the API said no
  2    usage error, or the backend could not be reached
  130  interrupted (Ctrl+C); running jobs are cancelled first
`;

const RUN_EXAMPLES = `
Examples:
  $ ioslab test run MyAppTests
  $ ioslab test run MyAppTests --workspace MyApp.xcworkspace --configuration Debug
  $ ioslab test run MyAppTests --only-testing MyAppTests/LoginTests --retries 2 -v
  $ ioslab test run MyAppTests --runtime 18.0 --runtime 17.5 --parallel 2 --junit results.xml

Passing --runtime or --model more than once runs the scheme on every combination (a matrix run).
The command waits for the result. It exits 0 only if every job completed, 1 if any failed or was
cancelled (or --timeout passed), 2 if the backend is unreachable, and 130 after Ctrl+C (which also
cancels the jobs).
`;

/** Where commands leave their exit code (commander discards action return values). */
interface RunState {
  exitCode: number;
  globals: GlobalOptions;
}

function buildProgram(io: CliIO, interrupt: InterruptState, state: RunState): Command {
  const program = new Command();

  // Settings are inherited by every subcommand created below, so they must come first.
  program
    .exitOverride()
    .configureOutput({
      writeOut: (text) => io.stdout.write(text),
      writeErr: (text) => io.stderr.write(text),
      getOutHasColors: () => false,
      getErrHasColors: () => false
    })
    .configureHelp({ showGlobalOptions: true, sortSubcommands: false })
    .showHelpAfterError("(run with --help for usage)");

  program
    .name("ioslab")
    .description("Drive a MobileLab backend from the terminal or CI: manage simulators, run Xcode tests and read the results.")
    .version(version, "-V, --version", "print the version number")
    .option("--api <url>", "backend address (default: $IOSLAB_API_URL or http://127.0.0.1:4000)")
    .option("--token <token>", "API token (default: $IOSLAB_API_TOKEN); prefer the environment variable, flags show up in process lists")
    .option("--json", "print machine-readable JSON instead of formatted text (commands that show data)")
    .option("--no-color", "never use colors (also honoured: NO_COLOR; output that is not a terminal is never colored)")
    .addHelpText("after", EXAMPLES);

  program.hook("preAction", (_root, action) => {
    state.globals = action.optsWithGlobals<GlobalOptions>();
  });

  /** Wraps a handler so it gets a context built from the global options, and its result becomes the exit code. */
  const run =
    <Args extends unknown[]>(handler: (ctx: CliContext, ...args: Args) => Promise<number | void>) =>
    async (...raw: unknown[]): Promise<void> => {
      const command = raw[raw.length - 1] as Command;
      const args = raw.slice(0, -1) as Args; // positional arguments..., then the command's own options
      const ctx = createContext(command.optsWithGlobals<GlobalOptions>(), io, interrupt);
      state.exitCode = (await handler(ctx, ...args)) ?? EXIT.OK;
    };

  // ---------------------------------------------------------------- overview

  program.command("status").description("Show the backend mode, capacity, devices and recent jobs").action(run((ctx) => statusCommand(ctx)));

  program
    .command("doctor")
    .description("Run the backend's environment checks (Xcode, runtimes, disk, capacity); exits 1 if any check fails")
    .action(run((ctx) => doctorCommand(ctx)));

  program
    .command("catalog")
    .description("List the runtimes and device types the backend can create (the values --runtime and --model accept)")
    .action(run((ctx) => catalogCommand(ctx)));

  // ---------------------------------------------------------------- devices

  program
    .command("devices")
    .alias("ls")
    .description("List devices with their runtime, status and backend")
    .action(run((ctx) => devicesCommand(ctx)));

  program
    .command("spawn")
    .description("Create and boot a simulator")
    .argument("[name]", "name for the new device (default: the device type and runtime)")
    .option("--runtime <runtime>", 'iOS runtime: a version like 18.0, "iOS 17.5" or a full identifier (default: newest installed; see "ioslab catalog")')
    .option("--model <device>", 'device type, e.g. "iPhone 15" (default: newest plain iPhone; see "ioslab catalog")')
    .option("--vm", "create an experimental, simulated VM instead of a simulator")
    .option("--no-wait", "return as soon as the device is created instead of waiting for it to finish booting")
    .action(run((ctx: CliContext, name: string | undefined, options: { runtime?: string; model?: string; vm?: boolean; wait?: boolean }) => spawnCommand(ctx, name, options)));

  const deviceArg = "<device>";
  const deviceHelp = "device id (a unique prefix is enough) or exact name; see \"ioslab devices\"";
  program.command("boot").description("Boot a device").argument(deviceArg, deviceHelp).action(run((ctx: CliContext, device: string) => bootCommand(ctx, device)));
  program.command("shutdown").description("Shut a device down").argument(deviceArg, deviceHelp).action(run((ctx: CliContext, device: string) => shutdownCommand(ctx, device)));
  program.command("rm").description("Shut down and delete a device").argument(deviceArg, deviceHelp).action(run((ctx: CliContext, device: string) => removeCommand(ctx, device)));

  // ---------------------------------------------------------------- tests

  const test = program.command("test").description("Run tests on simulators and inspect the jobs");

  test
    .command("run")
    .description("Run an Xcode scheme's tests, wait for the result and report it")
    .argument("<scheme>", "Xcode scheme to test")
    .option("--project <path>", "Xcode project (.xcodeproj); a relative path is resolved by the backend")
    .option("--workspace <path>", "Xcode workspace (.xcworkspace), instead of --project")
    .option("--dir <path>", "directory to run xcodebuild in when there is no project or workspace (Swift packages)")
    .option("--configuration <name>", "build configuration, e.g. Debug")
    .option("--only-testing <identifier>", "run only this test target, class or method (Target/Class/method); repeatable", collect)
    .option("--runtime <runtime>", 'iOS runtime, e.g. 18.0 or "iOS 17.5"; give it more than once to run a matrix', collect)
    .option("--model <device>", 'device type, e.g. "iPhone 15"; give it more than once to run a matrix', collect)
    .option("--parallel <n>", "matrix runs: at most n jobs at the same time", integer("--parallel", 1))
    .option("--retries <n>", "retry a failing job up to n times before giving up", integer("--retries", 0))
    .option("--no-provision", "do not create a simulator on demand; only use devices that are already ready")
    .option("--no-wait", "queue the job(s), print the id(s) and exit 0 without waiting")
    .option("--timeout <seconds>", "stop waiting after this long (the job keeps running on the backend) and exit 1", positiveNumber)
    .option("--junit <file>", "write the JUnit XML report here; with several jobs the job's short id is added to the file name")
    .option("-v, --verbose", "stream the raw xcodebuild output live")
    .addHelpText("after", RUN_EXAMPLES)
    .action(run((ctx: CliContext, scheme: string, options: RunOptions) => runCommand(ctx, scheme, options)));

  test
    .command("list")
    .description("List recent jobs, newest first")
    .addOption(new Option("--status <status>", "only jobs in this state").choices([...JOB_STATUSES]))
    .option("--limit <n>", "how many jobs to show (default 20)", integer("--limit", 1))
    .option("--run <id>", "only the jobs of this run (a matrix run's id)")
    .action(run((ctx: CliContext, options: ListOptions) => listCommand(ctx, options)));

  const jobArg = "<job>";
  const jobHelp = "job id (a unique prefix is enough); see \"ioslab test list\"";
  test.command("show").description("Show a job's details, summary and failed tests").argument(jobArg, jobHelp).action(run((ctx: CliContext, job: string) => showCommand(ctx, job)));
  test.command("cancel").description("Cancel a queued or running job").argument(jobArg, jobHelp).action(run((ctx: CliContext, job: string) => cancelCommand(ctx, job)));

  test
    .command("rerun")
    .description("Run a finished job again with the same settings and report the result")
    .argument(jobArg, jobHelp)
    .option("--no-wait", "queue the new job, print its id and exit 0 without waiting")
    .option("--timeout <seconds>", "stop waiting after this long (the job keeps running on the backend) and exit 1", positiveNumber)
    .option("--junit <file>", "write the JUnit XML report here")
    .option("-v, --verbose", "stream the raw xcodebuild output live")
    .action(run((ctx: CliContext, job: string, options: RerunOptions) => rerunCommand(ctx, job, options)));

  test
    .command("junit")
    .description("Print a job's JUnit XML report, or save it with -o")
    .argument(jobArg, jobHelp)
    .option("-o, --output <file>", "write to this file instead of stdout")
    .action(run((ctx: CliContext, job: string, options: { output?: string }) => junitCommand(ctx, job, options)));

  // ---------------------------------------------------------------- logs

  program
    .command("logs")
    .description("Show the tail of a job's raw xcodebuild output")
    .argument(jobArg, jobHelp)
    .option("-f, --follow", "keep streaming until the job finishes; exits 0 if it completed, 1 otherwise")
    .action(run((ctx: CliContext, job: string, options: { follow?: boolean }) => logsCommand(ctx, job, options)));

  // ---------------------------------------------------------------- VMs (experimental)

  const vm = program.command("vm").description("Experimental, simulated VMs: nothing real is started and they cannot run tests");
  const vmArg = "<vm>";
  const vmHelp = "VM id (a unique prefix is enough) or exact name; see \"ioslab vm list\"";

  vm.command("new")
    .description("Create a VM")
    .argument("<name>", "name for the VM")
    .option("--runtime <runtime>", "runtime label")
    .option("--cpu <n>", "number of vCPUs", integer("--cpu", 1))
    .option("--memory <gb>", "RAM in GB", integer("--memory", 1))
    .option("--disk <gb>", "disk size in GB", integer("--disk", 1))
    .action(run((ctx: CliContext, name: string, options: VmNewOptions) => vmNewCommand(ctx, name, options)));
  vm.command("boot").description("Boot a VM").argument(vmArg, vmHelp).action(run((ctx: CliContext, id: string) => vmBootCommand(ctx, id)));
  vm.command("backup")
    .description("Save a named state backup of a VM")
    .argument(vmArg, vmHelp)
    .argument("<backupName>", "name for the backup")
    .action(run((ctx: CliContext, id: string, name: string) => vmBackupCommand(ctx, id, name)));
  vm.command("restore")
    .description("Restore a VM to a named backup")
    .argument(vmArg, vmHelp)
    .argument("<backupName>", "name of an existing backup")
    .action(run((ctx: CliContext, id: string, name: string) => vmRestoreCommand(ctx, id, name)));
  vm.command("switch")
    .description("Change a VM's resources")
    .argument(vmArg, vmHelp)
    .option("--cpu <n>", "number of vCPUs", integer("--cpu", 1))
    .option("--memory <gb>", "RAM in GB", integer("--memory", 1))
    .option("--disk <gb>", "disk size in GB", integer("--disk", 1))
    .action(run((ctx: CliContext, id: string, options: VmSizeOptions) => vmSwitchCommand(ctx, id, options)));
  vm.command("list").alias("ls").description("List VMs").action(run((ctx) => vmListCommand(ctx)));

  return program;
}

function errorColor(io: CliIO, globals: GlobalOptions): boolean {
  const noColor = globals.color === false || (io.env.NO_COLOR !== undefined && io.env.NO_COLOR !== "");
  return Boolean(io.stderr.isTTY) && !noColor && io.env.TERM !== "dumb";
}

/** Prints an error (once) and returns the exit code it stands for. */
function reportError(error: unknown, io: CliIO, globals: GlobalOptions): number {
  // commander has already printed its own message for usage errors; --help and --version count as success.
  if (error instanceof CommanderError) return error.exitCode === 0 ? EXIT.OK : EXIT.USAGE;

  const red = createStyle(errorColor(io, globals)).red;
  if (error instanceof CliError) {
    if (error.message) io.stderr.write(`${red(error.message)}\n`);
    return error.exitCode;
  }
  const detail = io.env.IOSLAB_DEBUG && error instanceof Error && error.stack ? error.stack : error instanceof Error ? error.message : String(error);
  io.stderr.write(`${red(`Unexpected error: ${detail}`)}\n`);
  return EXIT.FAILED;
}

export { CliError } from "./errors";

/** `["node", "/path/to/ioslab", ...]` (a raw `process.argv`) is accepted too; only the arguments matter. */
function userArguments(argv: string[]): string[] {
  return argv.length >= 2 && /(^|[\\/])node(\.exe)?$/.test(argv[0]) ? argv.slice(2) : argv;
}

/**
 * Runs the CLI. `argv` is the arguments after the program name (`process.argv.slice(2)`). Returns the
 * exit code instead of exiting, and writes only through `io`, so it can be driven from tests.
 */
export async function main(argv: string[], partialIo: CliIOInput = {}): Promise<number> {
  const io = resolveIO(partialIo);
  const interrupt = new InterruptState(io.interrupt ?? new AbortController().signal);
  const state: RunState = { exitCode: EXIT.OK, globals: {} };
  const program = buildProgram(io, interrupt, state);

  // Ctrl+C: a command that manages its own cleanup (test run) claims it; otherwise just stop.
  let onAbort: (() => void) | undefined;
  const interrupted = new Promise<never>((_resolve, reject) => {
    onAbort = () => {
      if (!interrupt.claimed) reject(new CliError("Interrupted.", interrupt.exitCode));
    };
    if (interrupt.aborted) onAbort();
    else interrupt.signal.addEventListener("abort", onAbort, { once: true });
  });
  interrupted.catch(() => undefined);

  try {
    await Promise.race([program.parseAsync(userArguments(argv), { from: "user" }), interrupted]);
    return state.exitCode;
  } catch (error) {
    return reportError(error, io, state.globals);
  } finally {
    if (onAbort) interrupt.signal.removeEventListener("abort", onAbort);
  }
}
