import WebSocket from "ws";
import { ApiClient } from "./apiClient";
import { EngineEvent } from "./types";

export interface FeedQuery {
  jobId?: string;
  runId?: string;
  deviceId?: string;
  /** Include raw xcodebuild output lines (chatty). */
  output?: boolean;
  /** Ask the server to first replay this many recent lifecycle events, so nothing between submit and connect is missed. */
  replay?: number;
}

const HANDSHAKE_TIMEOUT_MS = 5000;

/**
 * A live event stream from `/ws/events`. It is strictly best effort: callers treat polling as the
 * source of truth and use `healthy` to decide whether the stream can be relied on for progress lines.
 * It never throws and never reconnects; if it dies, `healthy` turns false and that is that.
 */
export class EventFeed {
  private socket?: WebSocket;
  private isOpen = false;
  private isDead = false;
  /** Resolves true once connected, false if the connection failed or closed before that. Never rejects. */
  readonly opened: Promise<boolean>;
  failure?: string;

  constructor(client: ApiClient, query: FeedQuery, onEvent: (event: EngineEvent) => void) {
    this.opened = new Promise<boolean>((resolve) => {
      const fail = (reason: string) => {
        this.failure ??= reason;
        this.isDead = true;
        resolve(false);
      };
      try {
        const socket = new WebSocket(
          client.eventsUrl({ jobId: query.jobId, runId: query.runId, deviceId: query.deviceId, output: query.output ? 1 : undefined, replay: query.replay }),
          { headers: client.authHeaders(), handshakeTimeout: HANDSHAKE_TIMEOUT_MS }
        );
        this.socket = socket;
        socket.on("open", () => {
          this.isOpen = true;
          resolve(true);
        });
        socket.on("message", (data) => {
          try {
            onEvent(JSON.parse(data.toString()) as EngineEvent);
          } catch {
            /* a frame that is not an event is not worth stopping for */
          }
        });
        socket.on("unexpected-response", (_request, response) => {
          response.resume();
          fail(`the server refused the connection (HTTP ${response.statusCode})`);
          socket.terminate();
        });
        socket.on("error", (error) => fail(error.message));
        socket.on("close", () => fail("the connection closed"));
      } catch (error) {
        fail(error instanceof Error ? error.message : String(error));
      }
    });
  }

  /** Connected and not closed since. */
  get healthy(): boolean {
    return this.isOpen && !this.isDead;
  }

  close(): void {
    this.isDead = true;
    const socket = this.socket;
    if (!socket) return;
    if (socket.readyState === WebSocket.OPEN) socket.close();
    else if (socket.readyState === WebSocket.CONNECTING) socket.terminate();
  }
}
