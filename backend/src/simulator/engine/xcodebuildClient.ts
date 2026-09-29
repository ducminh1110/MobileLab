import path from "node:path";
import { DomainError } from "../../utils/errors";
import { CommandResult, CommandRunner } from "./commandRunner";

export interface XcodebuildRunParams {
  scheme: string;
  /** e.g. `platform=iOS Simulator,id=<udid>` */
  destination: string;
  projectPath?: string;
  workspacePath?: string;
  /** Working directory. Defaults to the project's folder, else the configured workspace root. */
  workingDirectory?: string;
  configuration?: string;
  onlyTesting?: string[];
  /** Must not exist yet: xcodebuild refuses to overwrite a result bundle. */
  resultBundlePath: string;
  logFile?: string;
  timeoutMs?: number;
  signal?: AbortSignal;
  onLine?: (line: string) => void;
}

/** xcodebuild takes flags and values from the same argv; a value that looks like a flag would be misread. */
export function assertSafeArgument(label: string, value: string): void {
  if (value.length === 0 || value.startsWith("-") || value.includes("\0") || value.includes("\n")) {
    throw new DomainError(`Invalid ${label}: "${value}"`, 400);
  }
}

export class XcodebuildClient {
  constructor(
    private readonly runner: CommandRunner,
    private readonly workspaceRoot: string
  ) {}

  buildTestArgs(params: XcodebuildRunParams): { args: string[]; cwd: string } {
    assertSafeArgument("scheme", params.scheme);
    if (params.configuration) assertSafeArgument("configuration", params.configuration);

    const resolve = (value: string, base: string) => (path.isAbsolute(value) ? value : path.resolve(base, value));
    const base = params.workingDirectory ? resolve(params.workingDirectory, this.workspaceRoot) : this.workspaceRoot;

    const args = ["test"];
    let cwd = base;
    if (params.workspacePath) {
      const workspace = resolve(params.workspacePath, base);
      args.push("-workspace", workspace);
      cwd = path.dirname(workspace);
    } else if (params.projectPath) {
      const project = resolve(params.projectPath, base);
      args.push("-project", project);
      cwd = path.dirname(project);
    }

    args.push("-scheme", params.scheme);
    if (params.configuration) args.push("-configuration", params.configuration);
    args.push("-destination", params.destination, "-resultBundlePath", params.resultBundlePath);
    for (const only of params.onlyTesting ?? []) {
      assertSafeArgument("only-testing filter", only);
      args.push(`-only-testing:${only}`);
    }
    return { args, cwd };
  }

  /**
   * Runs the tests once. No command-level retry: a failing run is reported to the scheduler, which owns
   * retry policy (and gives every attempt its own result bundle).
   */
  runTests(params: XcodebuildRunParams): Promise<CommandResult> {
    const { args, cwd } = this.buildTestArgs(params);
    return this.runner.run("xcodebuild", args, {
      cwd,
      timeoutMs: params.timeoutMs,
      signal: params.signal,
      logFile: params.logFile,
      onLine: params.onLine ? (line) => params.onLine!(line) : undefined
    });
  }

  async version(): Promise<string> {
    const result = await this.runner.run("xcodebuild", ["-version"], { timeoutMs: 15_000 });
    if (result.code !== 0) throw new Error(result.stderr.trim() || "xcodebuild -version failed");
    return result.stdout.trim().split("\n").join(" · ");
  }
}
