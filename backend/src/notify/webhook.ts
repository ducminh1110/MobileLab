import { EventHub } from "../core/eventHub";
import { Device, TestJob } from "../simulator/models/types";
import { errorMessage } from "../utils/errors";

export type FetchLike = (url: string, init: { method: string; headers: Record<string, string>; body: string; signal: AbortSignal }) => Promise<{ ok: boolean; status: number }>;

/** POSTs a JSON summary to IOSLAB_WEBHOOK_URL whenever a job finishes. The `text` field makes it work with Slack/Teams-style incoming webhooks as-is. */
export class WebhookNotifier {
  constructor(
    private readonly url: string,
    private readonly hub: EventHub,
    private readonly fetchImpl: FetchLike = fetch as unknown as FetchLike
  ) {}

  static describe(job: TestJob, device?: Device): string {
    const where = device ? ` on ${device.name}` : "";
    const seconds = job.durationMs !== undefined ? ` in ${Math.round(job.durationMs / 100) / 10}s` : "";
    const tests = job.summary ? ` (${job.summary.passed}/${job.summary.total} tests passed)` : "";
    const verdict = job.status === "completed" ? "passed" : job.status;
    return `${job.testTarget} ${verdict}${where}${seconds}${tests}`;
  }

  async notifyJobFinished(job: TestJob, device?: Device): Promise<void> {
    const text = WebhookNotifier.describe(job, device);
    try {
      const response = await this.fetchImpl(this.url, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ event: "job.finished", text, job, device }),
        signal: AbortSignal.timeout(5_000)
      });
      if (!response.ok) throw new Error(`webhook responded ${response.status}`);
    } catch (error) {
      this.hub.emit({ source: "system", type: "error", action: "webhook_failed", message: `Webhook delivery failed: ${errorMessage(error)}`, jobId: job.id });
    }
  }
}
