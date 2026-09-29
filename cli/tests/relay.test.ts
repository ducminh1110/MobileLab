import test from "node:test";
import assert from "node:assert/strict";
import { logLines, OutputRelay, overlapLength } from "../src/utils/outputRelay";

const HEADER = "# MobileLab job abc, attempt 1, iPhone (UDID)";

function relay() {
  const shown: string[] = [];
  return { shown, relay: new OutputRelay((_job, line) => shown.push(line)) };
}

test("logLines splits a log, drops the trailing newline and a truncated first line", () => {
  assert.deepEqual(logLines("a\nb\n", false), ["a", "b"]);
  assert.deepEqual(logLines("a\r\nb", false), ["a", "b"]);
  assert.deepEqual(logLines("", false), []);
  assert.deepEqual(logLines("partial\nb\nc\n", true), ["b", "c"]);
  assert.deepEqual(logLines("a\n\nb\n", false), ["a", "", "b"], "blank lines are lines");
});

test("overlapLength finds streamed lines that are already the end of the log", () => {
  assert.equal(overlapLength(["a", "b", "c"], ["c", "d"]), 1);
  assert.equal(overlapLength(["a", "b", "c"], ["b", "c", "d"]), 2);
  assert.equal(overlapLength(["a", "b", "c"], ["x", "y"]), 0);
  assert.equal(overlapLength(["a", "b"], []), 0);
  assert.equal(overlapLength([], ["a"]), 0);
});

test("overlapLength compares against the server's 2000 character cut", () => {
  const long = "z".repeat(2500);
  assert.equal(overlapLength(["a", long], [long.slice(0, 2000), "next"]), 1);
});

test("the log so far is printed first, then the stream, with lines seen twice printed once", () => {
  const { shown, relay: r } = relay();
  r.push("j", "c"); // arrived on the stream before the log was read
  r.push("j", "d"); // ...and this one after
  r.prime("j", [HEADER, "a", "b", "c"], 1);
  r.push("j", "e"); // live from now on
  assert.deepEqual(shown, [HEADER, "a", "b", "c", "d", "e"]);
});

test("stream lines wait until the log has been printed", () => {
  const { shown, relay: r } = relay();
  r.push("j", "new");
  assert.deepEqual(shown, []);
  r.prime("j", ["old"]);
  assert.deepEqual(shown, ["old", "new"]);
});

test("a stream message holding several lines counts as several lines", () => {
  const { shown, relay: r } = relay();
  r.prime("j", ["a"]);
  r.push("j", "b\nc");
  assert.deepEqual(shown, ["a", "b", "c"]);
  r.catchUp("j", ["a", "b", "c", "d"], false);
  assert.deepEqual(shown, ["a", "b", "c", "d"], "catch-up only adds what was not shown");
});

test("the banner line at the top of a log is shown once and does not shift line counts", () => {
  const { shown, relay: r } = relay();
  r.prime("j", [], 1); // the job had not started: nothing in the log yet
  r.push("j", "one");
  r.push("j", "two");
  r.catchUp("j", [HEADER, "one", "two", "three"], false, 1);
  assert.deepEqual(shown, ["one", "two", HEADER, "three"]);
});

test("catch-up without a stream prints the whole log, and later only the new lines", () => {
  const { shown, relay: r } = relay();
  r.catchUp("j", ["a", "b"], false);
  r.catchUp("j", ["a", "b", "c"], false);
  r.catchUp("j", ["a", "b", "c"], false);
  assert.deepEqual(shown, ["a", "b", "c"]);
});

test("a retry starts a new log: catch-up starts over", () => {
  const { shown, relay: r } = relay();
  r.catchUp("j", ["a1", "a2", "a3"], false, 1);
  r.catchUp("j", ["b1"], false, 2);
  assert.deepEqual(shown, ["a1", "a2", "a3", "b1"]);
});

test("a truncated tail is only used when nothing was shown yet", () => {
  const first = relay();
  first.relay.catchUp("j", ["tail1", "tail2"], true);
  assert.deepEqual(first.shown, ["tail1", "tail2"]);

  const second = relay();
  second.relay.push("j", "x");
  second.relay.prime("j", []);
  second.relay.catchUp("j", ["tail1", "tail2"], true);
  assert.deepEqual(second.shown, ["x"], "cannot line up a partial log with what was shown, so it adds nothing");
});

test("jobs are tracked separately", () => {
  const shown: Array<[string, string]> = [];
  const r = new OutputRelay((job, line) => shown.push([job, line]));
  r.prime("a", ["a1"]);
  r.push("b", "b-early");
  r.push("a", "a2");
  r.prime("b", []);
  assert.deepEqual(shown, [["a", "a1"], ["a", "a2"], ["b", "b-early"]]);
});
