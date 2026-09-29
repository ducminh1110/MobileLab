/**
 * A sleep that can be cut short. `wake()` (a live event arrived) or an abort signal (Ctrl+C) ends the
 * wait early. A `wake()` that lands while nobody is waiting is remembered, so it is never lost between polls.
 */
export class Waker {
  private release?: () => void;
  private pending = false;

  wake(): void {
    if (this.release) this.release();
    else this.pending = true;
  }

  wait(ms: number, signal?: AbortSignal): Promise<void> {
    if (this.pending) {
      this.pending = false;
      return Promise.resolve();
    }
    if (signal?.aborted || ms <= 0) return Promise.resolve();
    return new Promise<void>((resolve) => {
      const done = () => {
        clearTimeout(timer);
        signal?.removeEventListener("abort", done);
        this.release = undefined;
        resolve();
      };
      const timer = setTimeout(done, ms);
      signal?.addEventListener("abort", done, { once: true });
      this.release = done;
    });
  }
}
