export type EngineEventSource = "simctl" | "xcodebuild" | "scheduler" | "orchestrator" | "vm" | "system";
export type EngineEventType = "started" | "log" | "finished" | "error";

export interface EngineEvent {
  /** Monotonic per hub. Lets clients resume a stream and de-duplicate replayed history. */
  id: number;
  source: EngineEventSource;
  type: EngineEventType;
  action: string;
  message: string;
  timestamp: string;
  jobId?: string;
  deviceId?: string;
  runId?: string;
  metadata?: Record<string, unknown>;
}

export type EngineEventInput = Omit<EngineEvent, "id" | "timestamp">;

export interface EventFilter {
  jobId?: string;
  deviceId?: string;
  runId?: string;
  sinceId?: number;
  limit?: number;
}

type Handler = (event: EngineEvent) => void;

export function matchesFilter(event: EngineEvent, filter: EventFilter): boolean {
  if (filter.jobId && event.jobId !== filter.jobId) return false;
  if (filter.deviceId && event.deviceId !== filter.deviceId) return false;
  if (filter.runId && event.runId !== filter.runId) return false;
  if (filter.sinceId !== undefined && event.id <= filter.sinceId) return false;
  return true;
}

/**
 * In-process event stream. Lifecycle events are kept in a bounded history so a client that connects
 * late can catch up; raw tool output is "transient": it is fanned out live but never stored here
 * (the full text lives in the job's log artifact), otherwise a chatty build would evict everything
 * else from the history.
 */
export class EventHub {
  private seq = 0;
  private history: EngineEvent[] = [];
  private readonly handlers = new Set<Handler>();

  constructor(private readonly historyLimit = 2000) {}

  emit(input: EngineEventInput): EngineEvent {
    const event = this.build(input);
    this.history.push(event);
    if (this.history.length > this.historyLimit) {
      this.history.splice(0, this.history.length - this.historyLimit);
    }
    this.dispatch(event);
    return event;
  }

  emitTransient(input: EngineEventInput): EngineEvent {
    const event = this.build(input);
    this.dispatch(event);
    return event;
  }

  subscribe(handler: Handler): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }

  list(filter: EventFilter = {}): EngineEvent[] {
    const matched = this.history.filter((event) => matchesFilter(event, filter));
    return filter.limit ? matched.slice(-filter.limit) : matched;
  }

  get lastId(): number {
    return this.seq;
  }

  private build(input: EngineEventInput): EngineEvent {
    this.seq += 1;
    return { ...input, id: this.seq, timestamp: new Date().toISOString() };
  }

  private dispatch(event: EngineEvent): void {
    for (const handler of [...this.handlers]) {
      // A misbehaving subscriber (for example a socket that just closed) must never be able to
      // break the engine that emitted the event.
      try {
        handler(event);
      } catch {
        this.handlers.delete(handler);
      }
    }
  }
}
