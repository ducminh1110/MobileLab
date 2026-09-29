/** Process exit codes. `main()` returns one of these; only src/index.ts actually exits. */
export const EXIT = {
  OK: 0,
  /** The thing that was asked for failed: tests failed or were cancelled, a device operation failed, the API said 4xx/5xx. */
  FAILED: 1,
  /** The command line was wrong, or the backend could not be reached. */
  USAGE: 2,
  /** Ctrl+C (128 + SIGINT). */
  INTERRUPTED: 130
} as const;

/**
 * An error whose message is meant for the person at the terminal. Library code throws these instead of
 * calling `process.exit`; `main()` prints the message once and turns `exitCode` into its return value.
 */
export class CliError extends Error {
  constructor(
    message: string,
    public readonly exitCode: number = EXIT.FAILED
  ) {
    super(message);
    this.name = "CliError";
  }
}

/** Bad flags or arguments, detected before (or without) talking to the backend. */
export class UsageError extends CliError {
  constructor(message: string) {
    super(message, EXIT.USAGE);
    this.name = "UsageError";
  }
}

/** The backend answered with a 4xx/5xx. `message` is the server's own message. */
export class ApiError extends CliError {
  constructor(
    message: string,
    public readonly status: number,
    public readonly code?: string
  ) {
    super(message, EXIT.FAILED);
    this.name = "ApiError";
  }
}

export function unreachableMessage(baseUrl: string): string {
  return `Cannot reach the MobileLab backend at ${baseUrl}. Start it with "make dev" or set IOSLAB_API_URL.`;
}

/** No connection, DNS failure, timeout, or a server that is not speaking the MobileLab API. */
export class BackendUnreachableError extends CliError {
  constructor(
    public readonly baseUrl: string,
    message: string = unreachableMessage(baseUrl)
  ) {
    super(message, EXIT.USAGE);
    this.name = "BackendUnreachableError";
  }
}

export function errorText(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
