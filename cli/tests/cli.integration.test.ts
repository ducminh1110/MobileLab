import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import http from "node:http";
import { AddressInfo } from "node:net";
import os from "node:os";
import path from "node:path";
import { ANSI, CliResult, closedPortUrl, runCli, startProxy, waitFor, withBackend } from "./helpers";
import { main } from "../src/cli";
import type { TestJob, TestRunView } from "../src/client/types";

const RT_18 = "com.apple.CoreSimulator.SimRuntime.iOS-18-0";
const RT_17 = "com.apple.CoreSimulator.SimRuntime.iOS-17-5";
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

const count = (text: string, pattern: RegExp): number => (text.match(new RegExp(pattern.source, "g")) ?? []).length;
const parse = <T>(result: CliResult): T => JSON.parse(result.stdout) as T;
const tmpDir = () => fs.mkdtempSync(path.join(os.tmpdir(), "ioslab-cli-out-"));

// ---------------------------------------------------------------------------- test run

test("test run: a passing scheme is followed live, ends in a results table and exits 0", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);

    assert.match(result.stdout, /Results: AppTests/);
    assert.match(result.stdout, /JOB\s+DEVICE\s+STATUS\s+PASSED\s+DURATION\s+ERROR/);
    assert.match(result.stdout, /iPhone 15 \(iOS 18\.2\)\s+completed\s+8\/8/, "the device that ran it, its status and passed/total");
    assert.match(result.stdout, /The job passed\./);

    // Live status lines, each reported once even though both the socket and polling saw them.
    assert.equal(count(result.stdout, /Running AppTests on iPhone 15/), 1);
    assert.equal(count(result.stdout, /AppTests passed/), 1);

    assert.match(result.stderr, /DEMO MODE/, "a green run against a demo backend must say it proves nothing");
    assert.doesNotMatch(result.stdout + result.stderr, ANSI, "no ANSI when stdout is not a terminal");
    assert.equal(backend.services.orchestrator.listJobs().length, 1);
  });
});

test("test run: legacy `ioslab test run <target>` still works and creates a job for that scheme", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    const [job] = backend.services.orchestrator.listJobs();
    assert.equal(job.testTarget, "AppTests");
    assert.equal(job.status, "completed");
  });
});

test("test run: a failing scheme exits 1 and shows each failed test with its message", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "FailApp"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stdout, /failed\s+6\/8\s+.*2 of 8 tests failed/);
    assert.match(result.stdout, /Failures/);
    assert.match(result.stdout, /✖ FailAppTests\.LoginTests\.testInvalidPassword/);
    assert.match(result.stdout, /✖ FailAppTests\.CheckoutTests\.testPaymentDeclined/);
    assert.match(result.stdout, /XCTAssertEqual failed: \("Welcome"\) is not equal to \("Error"\)/, "the assertion message, not just the test name");
    assert.match(result.stdout, /The job did not pass \(1 failed\)\./);
  });
});

test("test run: a build error (unknown scheme) exits 1 and says why", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "MissingApp"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stdout, /The build failed before any test ran/);
    assert.match(result.stdout, /does not contain a scheme named "MissingApp"/);
  });
});

test("test run: --retries lets a flaky scheme pass; without it the same scheme fails", async () => {
  await withBackend(async (backend) => {
    const retried = await runCli(["test", "run", "FlakyApp", "--retries", "1"], { url: backend.url });
    assert.equal(retried.code, 0, retried.stdout);
    assert.match(retried.stdout, /retry 1\/1/);
    assert.match(retried.stdout, /Running FlakyApp on .* \(attempt 2\)/);
    assert.match(retried.stdout, /completed\s+8\/8/);

    const [job] = backend.services.orchestrator.listJobs();
    assert.equal(job.attempts, 2);
  });
  await withBackend(async (backend) => {
    const once = await runCli(["test", "run", "FlakyApp"], { url: backend.url });
    assert.equal(once.code, 1);
  });
});

test("test run: --json prints one parseable document and nothing else on stdout", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "FailApp", "--json"], { url: backend.url });
    assert.equal(result.code, 1);
    const doc = parse<{ ok: boolean; jobs: TestJob[]; results: Record<string, { cases: Array<{ status: string; message?: string }> }> }>(result);
    assert.equal(doc.ok, false);
    assert.equal(doc.jobs.length, 1);
    assert.equal(doc.jobs[0].status, "failed");
    const failedCases = doc.results[doc.jobs[0].id].cases.filter((c) => c.status === "failed");
    assert.equal(failedCases.length, 2);
    assert.match(failedCases[0].message ?? "", /XCTAssertEqual/);
  });
});

test("test run: two --runtime values run a matrix (POST /runs) with one job each", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "17.5"], { url: backend.url });
    assert.equal(result.code, 0, result.stdout + result.stderr);

    const jobs = backend.services.orchestrator.listJobs();
    assert.equal(jobs.length, 2);
    assert.deepEqual(jobs.map((j) => j.requiredRuntime).sort(), [RT_17, RT_18].sort());
    assert.equal(backend.services.orchestrator.listRuns().length, 1, "a run groups the jobs");

    assert.match(result.stdout, /Run of AppTests: 2 jobs/, "the run-level event only the live stream carries");
    assert.match(result.stdout, /All 2 jobs passed\./);
    const rows = result.stdout.split("\n").filter((line) => /\bcompleted\b/.test(line) && /8\/8/.test(line));
    assert.equal(rows.length, 2, "one results row per job");
    assert.match(result.stdout, /\[[0-9a-f]{8}\] Running AppTests on/, "with several jobs, live lines say which job they belong to");
  });
});

test("test run: --model twice is a matrix too, --parallel becomes maxParallel, and the run is in the JSON", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--model", "iPhone 15", "--model", "iPad (10th generation)", "--parallel", "1", "--json"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    const doc = parse<{ run: TestRunView; jobs: TestJob[] }>(result);
    assert.equal(doc.jobs.length, 2);
    assert.equal(doc.run.maxParallel, 1);
    assert.equal(doc.run.status, "passed");
    assert.deepEqual(doc.jobs.map((j) => j.requiredModelId?.split(".").pop()).sort(), ["iPad-10th-generation", "iPhone-15"]);
  });
});

test("test run: a single --runtime is a single job (POST /tests/run), repeated identical values too", async () => {
  await withBackend(async (backend) => {
    const one = await runCli(["test", "run", "AppTests", "--runtime", "17.5", "--json"], { url: backend.url });
    assert.equal(one.code, 0, one.stderr);
    const doc = parse<{ run?: TestRunView; jobs: TestJob[] }>(one);
    assert.equal(doc.run, undefined);
    assert.equal(doc.jobs[0].requiredRuntime, RT_17);

    const twice = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "18.0", "--json"], { url: backend.url });
    assert.equal(twice.code, 0);
    assert.equal(parse<{ jobs: TestJob[] }>(twice).jobs.length, 1);
    assert.equal(backend.services.orchestrator.listRuns().length, 0, "no run was created");
  });
});

test("test run: a matrix with a failing scheme exits 1 and names the device that failed", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "FailApp", "--runtime", "18.0", "--runtime", "17.5"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stdout, /Failures on iPhone 15 \(iOS 1[78]\.\d\) \(job [0-9a-f]{8}\)/);
    assert.match(result.stdout, /2 of 2 jobs did not pass \(2 failed\)/);
  });
});

test("test run: an unknown runtime is the server's 400, exit 1, with its message", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--runtime", "99.9"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stderr, /Unknown runtime "99\.9"\. Available: iOS 18\.2, iOS 18\.0, iOS 17\.5/);
  });
});

test("test run: --junit writes the report; several jobs get the job's short id in the file name", async () => {
  await withBackend(async (backend) => {
    const dir = tmpDir();
    try {
      const single = await runCli(["test", "run", "AppTests", "--junit", path.join(dir, "nested", "results.xml")], { url: backend.url });
      assert.equal(single.code, 0, single.stderr);
      const xml = fs.readFileSync(path.join(dir, "nested", "results.xml"), "utf-8");
      assert.match(xml, /^<\?xml version="1.0"/);
      assert.match(xml, /<testsuite name="AppTests" tests="8" failures="0"/);
      assert.match(single.stdout, /Wrote JUnit report: .*results\.xml/);

      // relative paths are relative to the working directory
      const matrix = await runCli(["test", "run", "FailApp", "--runtime", "18.0", "--runtime", "17.5", "--junit", "junit.xml"], { url: backend.url, cwd: dir });
      assert.equal(matrix.code, 1);
      const written = fs.readdirSync(dir).filter((f) => /^junit-[0-9a-f]{8}\.xml$/.test(f));
      assert.equal(written.length, 2);
      for (const file of written) {
        const body = fs.readFileSync(path.join(dir, file), "utf-8");
        assert.match(body, /<failure message=/, "a failing run's report carries the failures");
        assert.ok(backend.services.orchestrator.listJobs().some((j) => file.includes(j.id.slice(0, 8))), "the suffix is a real job's short id");
      }
      assert.ok(!fs.existsSync(path.join(dir, "junit.xml")), "with several jobs there is no un-suffixed file");
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

test("test run: --junit cannot be combined with --no-wait (usage error, nothing is submitted)", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--no-wait", "--junit", "x.xml"], { url: backend.url });
    assert.equal(result.code, 2);
    assert.match(result.stderr, /--junit needs the results/);
    assert.equal(backend.services.orchestrator.listJobs().length, 0);
  });
});

test("test run: --project together with --workspace is a usage error, exit 2, before any request", async () => {
  const result = await runCli(["test", "run", "AppTests", "--project", "A.xcodeproj", "--workspace", "A.xcworkspace"], { url: await closedPortUrl() });
  assert.equal(result.code, 2);
  assert.match(result.stderr, /either --project or --workspace/);
  assert.doesNotMatch(result.stderr, /Cannot reach/);
});

test("test run: --project/--workspace/--dir/--configuration/--only-testing reach the job", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(
      ["test", "run", "AppTests", "--workspace", "App.xcworkspace", "--dir", "app", "--configuration", "Debug", "--only-testing", "AppTests/LoginTests", "--only-testing", "AppTests/CartTests", "--no-provision", "--no-wait"],
      { url: backend.url }
    );
    assert.equal(result.code, 0, result.stderr);
    const [job] = backend.services.orchestrator.listJobs();
    assert.equal(job.workspacePath, "App.xcworkspace");
    assert.equal(job.workingDirectory, "app");
    assert.equal(job.configuration, "Debug");
    assert.deepEqual(job.onlyTesting, ["AppTests/LoginTests", "AppTests/CartTests"]);
    assert.equal(job.autoProvision, false, "--no-provision");
  });
});

test("test run: --no-wait prints the job id and exits 0 without waiting", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "SlowApp", "--no-wait"], { url: backend.url });
    assert.equal(result.code, 0);
    const [job] = backend.services.orchestrator.listJobs();
    assert.match(result.stdout, new RegExp(`Queued job ${job.id} \\(SlowApp\\)`), "the full job id");
    assert.doesNotMatch(result.stdout, /Results/);

    const matrix = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "17.5", "--no-wait", "--json"], { url: backend.url });
    assert.equal(matrix.code, 0);
    const doc = parse<{ run: TestRunView; jobs: TestJob[] }>(matrix);
    assert.equal(doc.jobs.length, 2);
    assert.ok(UUID.test(doc.run.id));
  });
});

test("test run: --parallel without a matrix only warns", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--parallel", "2"], { url: backend.url });
    assert.equal(result.code, 0);
    assert.match(result.stderr, /--parallel only applies when more than one --runtime or --model/);
  });
});

test("test run: --timeout stops waiting, exits 1, and leaves the job running", async () => {
  await withBackend(
    async (backend) => {
      const result = await runCli(["test", "run", "SlowApp", "--timeout", "0.5"], { url: backend.url });
      assert.equal(result.code, 1);
      assert.match(result.stderr, /Timed out after 0\.5s; 1 job is still unfinished on the backend/);
      assert.match(result.stdout, /running/);
      const [job] = backend.services.orchestrator.listJobs();
      assert.ok(["queued", "running"].includes(job.status), `the job was not cancelled (it is ${job.status})`);
    },
    { mockLatencyMs: 40 }
  );
});

test("test run: Ctrl+C cancels the job, reports it and exits 130", async () => {
  await withBackend(
    async (backend) => {
      const controller = new AbortController();
      const running = runCli(["test", "run", "SlowApp"], { url: backend.url, interrupt: controller.signal });
      await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");
      controller.abort(130);
      const result = await running;

      assert.equal(result.code, 130);
      assert.match(result.stderr, /Interrupted\. Asking the backend to cancel the job/);
      assert.match(result.stderr, /Cancelled job [0-9a-f]{8} \(SlowApp\)/);
      assert.equal(backend.services.orchestrator.listJobs()[0].status, "cancelled", "the backend really stopped the job");
    },
    { mockLatencyMs: 40 }
  );
});

test("test run: Ctrl+C on a matrix cancels the whole run", async () => {
  await withBackend(
    async (backend) => {
      const controller = new AbortController();
      const running = runCli(["test", "run", "SlowApp", "--runtime", "18.0", "--runtime", "17.5", "--parallel", "1"], { url: backend.url, interrupt: controller.signal });
      await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "a job to be running");
      controller.abort(130);
      const result = await running;

      assert.equal(result.code, 130);
      assert.match(result.stderr, /Cancelled run [0-9a-f]{8} \(2 jobs cancelled/);
      assert.deepEqual(backend.services.orchestrator.listJobs().map((j) => j.status), ["cancelled", "cancelled"], "including the job that never started");
    },
    { mockLatencyMs: 40 }
  );
});

test("test run: -v streams the raw xcodebuild output live, each line once", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "-v"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.equal(count(result.stdout, /Test Case '-\[AppTestsTests\.LoginTests testValidCredentials\]' passed/), 1);
    assert.equal(count(result.stdout, /Test Case '-\[AppTestsTests\.SettingsTests testLogout\]' passed/), 1);
    assert.equal(count(result.stdout, /\*\* TEST SUCCEEDED \*\*/), 1);
    assert.equal(count(result.stdout, /Executed 8 tests/), 1);
    assert.ok(result.stdout.indexOf("** TEST SUCCEEDED **") < result.stdout.indexOf("Results: AppTests"), "output comes before the results");
  });
});

test("test run: -v with several jobs prefixes each output line with its job", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "17.5", "-v"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.equal(count(result.stdout, /\[[0-9a-f]{8}\] .*testLogout\]' passed/), 2);
    assert.equal(count(result.stdout, /\[[0-9a-f]{8}\] \*\* TEST SUCCEEDED \*\*/), 2);
  });
});

test("test run: -v with --json keeps stdout clean (raw output goes to stderr)", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["test", "run", "AppTests", "-v", "--json"], { url: backend.url });
    assert.equal(result.code, 0);
    assert.equal(parse<{ ok: boolean }>(result).ok, true);
    assert.match(result.stderr, /TEST SUCCEEDED/);
  });
});

// ---------------------------------------------------------------------------- the live stream is optional

for (const mode of ["refuse", "drop"] as const) {
  test(`test run: finishes correctly when the WebSocket cannot connect (${mode}), reporting progress by polling`, async () => {
    await withBackend(async (backend) => {
      const proxy = await startProxy(backend.port, mode);
      try {
        const result = await runCli(["test", "run", "FailApp", "--retries", "1"], { url: proxy.url });
        assert.equal(result.code, 1, result.stdout + result.stderr);
        assert.match(result.stdout, /Queued FailApp/);
        assert.match(result.stdout, /Running FailApp on /);
        assert.equal(count(result.stdout, /Running FailApp on .* \(attempt 2\)/), 1);
        assert.match(result.stdout, /FailApp failed: 2 of 8 tests failed/);
        assert.match(result.stdout, /✖ FailAppTests\.LoginTests\.testInvalidPassword/);
        assert.equal(count(result.stdout, /FailApp failed: 2 of 8 tests failed/), 1, "the final line is not printed twice");

        const passing = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "17.5"], { url: proxy.url });
        assert.equal(passing.code, 0, passing.stdout + passing.stderr);
        assert.match(passing.stdout, /All 2 jobs passed\./);
        assert.doesNotMatch(passing.stdout, /Run of AppTests: 2 jobs/, "that line only exists on the socket");
      } finally {
        await proxy.stop();
      }
    });
  });
}

test("test run -v: without the live stream the log is fetched once the job is done", async () => {
  await withBackend(async (backend) => {
    const proxy = await startProxy(backend.port, "refuse");
    try {
      const result = await runCli(["test", "run", "AppTests", "-v"], { url: proxy.url });
      assert.equal(result.code, 0, result.stderr);
      assert.match(result.stderr, /live event stream unavailable/);
      assert.equal(count(result.stdout, /testLogout\]' passed/), 1);
      assert.equal(count(result.stdout, /\*\* TEST SUCCEEDED \*\*/), 1);
    } finally {
      await proxy.stop();
    }
  });
});

test("test run: a backend that disappears mid-run is exit 2 with the usual hint", async () => {
  await withBackend(
    async (backend) => {
      const proxy = await startProxy(backend.port, "drop");
      const running = runCli(["test", "run", "SlowApp"], { url: proxy.url });
      await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");
      await proxy.stop();
      const result = await running;
      assert.equal(result.code, 2);
      assert.equal(result.stderr.trim().split("\n").pop(), `Cannot reach the MobileLab backend at ${proxy.url}. Start it with "make dev" or set IOSLAB_API_URL.`);
    },
    { mockLatencyMs: 40 }
  );
});

// ---------------------------------------------------------------------------- other test subcommands

test("test list / show / junit / cancel / rerun work on job id prefixes", async () => {
  await withBackend(async (backend) => {
    await runCli(["test", "run", "AppTests"], { url: backend.url });
    await runCli(["test", "run", "FailApp"], { url: backend.url });
    const jobs = backend.services.orchestrator.listJobs();
    const failed = jobs.find((j) => j.testTarget === "FailApp")!;
    const passed = jobs.find((j) => j.testTarget === "AppTests")!;

    const list = await runCli(["test", "list"], { url: backend.url });
    assert.equal(list.code, 0);
    assert.match(list.stdout, /ID\s+SCHEME\s+STATUS\s+DEVICE\s+TESTS\s+DURATION\s+CREATED/);
    assert.match(list.stdout, new RegExp(`${failed.id.slice(0, 8)}\\s+FailApp\\s+failed`));
    assert.match(list.stdout, new RegExp(`${passed.id.slice(0, 8)}\\s+AppTests\\s+completed\\s+.*8/8`));

    const onlyFailed = await runCli(["test", "list", "--status", "failed", "--limit", "5"], { url: backend.url });
    assert.match(onlyFailed.stdout, /FailApp/);
    assert.doesNotMatch(onlyFailed.stdout, /AppTests\s+completed/);
    assert.equal((await runCli(["test", "list", "--status", "queued"], { url: backend.url })).stdout.trim(), "No queued jobs.");

    const show = await runCli(["test", "show", failed.id.slice(0, 6)], { url: backend.url });
    assert.equal(show.code, 0, show.stderr);
    assert.match(show.stdout, new RegExp(`Job\\s+${failed.id}`));
    assert.match(show.stdout, /Scheme\s+FailApp/);
    assert.match(show.stdout, /Status\s+failed/);
    assert.match(show.stdout, /Tests\s+8 total, 6 passed, 2 failed, 0 skipped/);
    assert.match(show.stdout, /✖ FailAppTests\.LoginTests\.testInvalidPassword/);
    const showJson = parse<{ job: TestJob; results: { cases: unknown[] } }>(await runCli(["test", "show", failed.id, "--json"], { url: backend.url }));
    assert.equal(showJson.job.id, failed.id);
    assert.equal(showJson.results.cases.length, 8);

    const junit = await runCli(["test", "junit", failed.id.slice(0, 8)], { url: backend.url });
    assert.equal(junit.code, 0);
    assert.match(junit.stdout, /^<\?xml/);
    assert.match(junit.stdout, /<failure message=/);
    assert.ok(junit.stdout.endsWith("\n"));

    const dir = tmpDir();
    try {
      const saved = await runCli(["test", "junit", failed.id.slice(0, 8), "-o", "out/report.xml"], { url: backend.url, cwd: dir });
      assert.equal(saved.code, 0);
      assert.match(fs.readFileSync(path.join(dir, "out", "report.xml"), "utf-8"), /<testsuites/);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }

    const cancelFinished = await runCli(["test", "cancel", passed.id.slice(0, 8)], { url: backend.url });
    assert.equal(cancelFinished.code, 1);
    assert.match(cancelFinished.stderr, /Job is already completed\./);

    const rerun = await runCli(["test", "rerun", failed.id.slice(0, 8)], { url: backend.url });
    assert.equal(rerun.code, 1, "the same scheme fails again");
    assert.equal(backend.services.orchestrator.listJobs().length, 3);
    assert.match(rerun.stdout, /✖ FailAppTests\.LoginTests\.testInvalidPassword/);

    const queued = await runCli(["test", "rerun", passed.id.slice(0, 8), "--no-wait"], { url: backend.url });
    assert.equal(queued.code, 0);
    assert.match(queued.stdout, /Queued job [0-9a-f-]{36} \(AppTests\)/);
  });
});

test("test cancel stops a running job; test rerun refuses a job that has not finished", async () => {
  await withBackend(
    async (backend) => {
      await runCli(["test", "run", "SlowApp", "--no-wait"], { url: backend.url });
      const job = await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");

      const rerun = await runCli(["test", "rerun", job.id.slice(0, 8)], { url: backend.url });
      assert.equal(rerun.code, 1);
      assert.match(rerun.stderr, /Job is still running\./);

      const cancel = await runCli(["test", "cancel", job.id.slice(0, 8)], { url: backend.url });
      assert.equal(cancel.code, 0, cancel.stderr);
      assert.match(cancel.stdout, /Cancelled job [0-9a-f]{8} \(SlowApp\); it is now cancelled/);
      assert.equal(backend.services.orchestrator.getJob(job.id)!.status, "cancelled");
    },
    { mockLatencyMs: 40 }
  );
});

test("job commands with an unknown id: the server's 404 with a hint, exit 1", async () => {
  await withBackend(async (backend) => {
    for (const args of [["test", "show", "nope"], ["test", "cancel", "nope"], ["test", "junit", "nope"], ["logs", "nope"], ["test", "rerun", "nope"]]) {
      const result = await runCli(args, { url: backend.url });
      assert.equal(result.code, 1, args.join(" "));
      assert.match(result.stderr, /Job not found: nope/, args.join(" "));
      assert.match(result.stderr, /ioslab test list/, args.join(" "));
    }
  });
});

test("a job id prefix shared by several jobs is ambiguous: candidates are listed, exit 1", async () => {
  await withBackend(async (backend) => {
    // Two ids sharing a prefix cannot be forced from the outside, so make the backend's ids collide by searching.
    const orchestrator = backend.services.orchestrator;
    let pair: [TestJob, TestJob] | undefined;
    const seen = new Map<string, TestJob>();
    for (let i = 0; i < 400 && !pair; i += 1) {
      const job = await orchestrator.enqueueTest({ testTarget: "AppTests", autoProvision: false });
      const key = job.id[0];
      const other = seen.get(key);
      if (other) pair = [other, job];
      else seen.set(key, job);
    }
    assert.ok(pair, "found two jobs whose ids start with the same character");
    const result = await runCli(["test", "show", pair![0].id[0]], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stderr, /matches \d+ jobs\. Use a longer id prefix to pick one:/);
    assert.ok(result.stderr.includes(pair![0].id.slice(0, 8)) && result.stderr.includes(pair![1].id.slice(0, 8)), "both candidates are listed");
  });
});

// ---------------------------------------------------------------------------- logs

test("logs prints the output of a finished job", async () => {
  await withBackend(async (backend) => {
    await runCli(["test", "run", "AppTests"], { url: backend.url });
    const [job] = backend.services.orchestrator.listJobs();
    const result = await runCli(["logs", job.id.slice(0, 8)], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /^# MobileLab job /);
    assert.match(result.stdout, /Test Case '-\[AppTestsTests\.LoginTests testValidCredentials\]' passed/);
    assert.match(result.stdout, /\*\* TEST SUCCEEDED \*\*/);

    const json = parse<{ text: string; attempt: number }>(await runCli(["logs", job.id, "--json"], { url: backend.url }));
    assert.match(json.text, /TEST SUCCEEDED/);
  });
});

test("logs does not filter by substring: it asks the server for that one job", async () => {
  await withBackend(async (backend) => {
    await runCli(["test", "run", "AppTests"], { url: backend.url });
    await runCli(["test", "run", "OtherApp"], { url: backend.url });
    const jobs = backend.services.orchestrator.listJobs();
    const other = jobs.find((j) => j.testTarget === "OtherApp")!;
    const result = await runCli(["logs", other.id.slice(0, 8)], { url: backend.url });
    assert.match(result.stdout, /-scheme OtherApp /);
    assert.doesNotMatch(result.stdout, /-scheme AppTests /);
  });
});

test("logs on a job that has not produced output says so", async () => {
  await withBackend(async (backend) => {
    const job = await backend.services.orchestrator.enqueueTest({ testTarget: "AppTests", autoProvision: false });
    const result = await runCli(["logs", job.id], { url: backend.url });
    assert.equal(result.code, 0);
    assert.equal(result.stdout, "");
    assert.match(result.stderr, /no output yet/);
  });
});

test("logs -f follows a running job to the end without repeating or dropping lines; exit 0 when it passed", async () => {
  await withBackend(
    async (backend) => {
      await runCli(["test", "run", "SlowApp", "--no-wait"], { url: backend.url });
      const job = await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");
      const result = await runCli(["logs", "-f", job.id.slice(0, 8)], { url: backend.url });

      assert.equal(result.code, 0, result.stderr);
      assert.match(result.stderr, /Job [0-9a-f]{8} passed/);
      const tests = ["testValidCredentials", "testInvalidPassword", "testEmptyUsername", "testAddToCart", "testApplyCoupon", "testPaymentDeclined", "testToggleDarkMode", "testLogout"];
      for (const name of tests) assert.equal(count(result.stdout, new RegExp(`${name}\\]' passed`)), 1, `${name} appears exactly once`);
      assert.equal(count(result.stdout, /\*\* TEST SUCCEEDED \*\*/), 1);
      assert.equal(count(result.stdout, /^# MobileLab job /m), 1, "the banner once");
      const order = tests.map((name) => result.stdout.indexOf(`${name}]' passed`));
      assert.deepEqual([...order].sort((a, b) => a - b), order, "in order");
    },
    { mockLatencyMs: 40 }
  );
});

test("logs -f exits 1 when the job failed, and 0 immediately for one that already finished", async () => {
  await withBackend(async (backend) => {
    await runCli(["test", "run", "FailApp"], { url: backend.url });
    const [job] = backend.services.orchestrator.listJobs();
    const started = Date.now();
    const result = await runCli(["logs", "-f", job.id.slice(0, 8)], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stderr, /Job [0-9a-f]{8} failed: 2 of 8 tests failed/);
    assert.equal(count(result.stdout, /\*\* TEST FAILED \*\*/), 1);
    assert.ok(Date.now() - started < 1500, "an already finished job does not wait");
  });
});

test("logs -f works without the live stream too (it polls the log)", async () => {
  await withBackend(
    async (backend) => {
      const proxy = await startProxy(backend.port, "refuse");
      try {
        await runCli(["test", "run", "SlowApp", "--no-wait"], { url: backend.url });
        const job = await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");
        const result = await runCli(["logs", "-f", job.id.slice(0, 8)], { url: proxy.url });
        assert.equal(result.code, 0, result.stderr);
        assert.equal(count(result.stdout, /testLogout\]' passed/), 1);
        assert.equal(count(result.stdout, /\*\* TEST SUCCEEDED \*\*/), 1);
        assert.equal(count(result.stdout, /testAddToCart\]' passed/), 1);
      } finally {
        await proxy.stop();
      }
    },
    { mockLatencyMs: 40 }
  );
});

test("logs -f: Ctrl+C stops watching (exit 130) but leaves the job alone", async () => {
  await withBackend(
    async (backend) => {
      await runCli(["test", "run", "SlowApp", "--no-wait"], { url: backend.url });
      const job = await waitFor(() => backend.services.orchestrator.listJobs().find((j) => j.status === "running"), 5000, "the job to be running");
      const controller = new AbortController();
      const following = runCli(["logs", "-f", job.id], { url: backend.url, interrupt: controller.signal });
      await new Promise((resolve) => setTimeout(resolve, 200));
      controller.abort(130);
      const result = await following;
      assert.equal(result.code, 130);
      assert.notEqual(backend.services.orchestrator.getJob(job.id)!.status, "cancelled");
    },
    { mockLatencyMs: 40 }
  );
});

test("logs -f with --json is a usage error", async () => {
  const result = await runCli(["logs", "-f", "abc", "--json"], { url: await closedPortUrl() });
  assert.equal(result.code, 2);
  assert.match(result.stderr, /--json cannot be combined with --follow/);
});

// ---------------------------------------------------------------------------- status, doctor, catalog

test("status: a readable summary with a loud demo banner, capacity, devices and recent jobs", async () => {
  await withBackend(async (backend) => {
    await runCli(["spawn", "Status Phone", "--runtime", "18.0"], { url: backend.url });
    await runCli(["test", "run", "AppTests"], { url: backend.url });
    await runCli(["test", "run", "FailApp"], { url: backend.url });

    const result = await runCli(["status"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /MobileLab backend http:\/\/127\.0\.0\.1:\d+\s+v\d+\.\d+\.\d+/);
    assert.match(result.stdout, /^!! DEMO MODE: .* NOT real !!$/m);
    assert.match(result.stdout, /Capacity: 1\/4 units in use/);
    assert.match(result.stdout, /Devices \(1\)/);
    assert.match(result.stdout, /ID\s+NAME\s+RUNTIME\s+STATUS\s+BACKEND\s+EPHEMERAL/);
    assert.match(result.stdout, /Status Phone\s+iOS 18\.0\s+ready\s+simctl\s+no/);
    assert.match(result.stdout, /Recent jobs \(2\)/);
    assert.match(result.stdout, /ID\s+SCHEME\s+STATUS\s+DEVICE\s+TESTS\s+DURATION\s+CREATED/);
    assert.match(result.stdout, /FailApp\s+failed\s+Status Phone\s+6\/8/);
    assert.match(result.stdout, /AppTests\s+completed\s+Status Phone\s+8\/8/);
    assert.ok(!result.stdout.trimStart().startsWith("{"), "not a JSON dump");
  });
});

test("status --json prints the raw combined data as one JSON document", async () => {
  await withBackend(async (backend) => {
    await runCli(["spawn", "J Phone"], { url: backend.url });
    await runCli(["test", "run", "AppTests"], { url: backend.url });
    const result = await runCli(["status", "--json"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    const doc = parse<{ health: { mode: string }; capabilities: { auth: { required: boolean } }; capacity: { maxLoad: number; load: number }; devices: Array<{ name: string }>; jobs: TestJob[] }>(result);
    assert.equal(doc.health.mode, "demo");
    assert.equal(doc.capacity.maxLoad, 4);
    assert.equal(doc.capacity.load, 1);
    assert.equal(doc.devices[0].name, "J Phone");
    assert.equal(doc.jobs.length, 1);
    assert.equal(doc.capabilities.auth.required, false);
    const before = parse<{ devices: unknown[] }>(await runCli(["--json", "status"], { url: backend.url }));
    assert.equal(before.devices.length, 1, "--json works before the command as well as after it");
  });
});

test("doctor in demo mode exits 0, reports what the backend found, and never claims Xcode passed", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["doctor"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /! warn\s+Execution mode: Demo mode/);
    assert.match(result.stdout, /Fix: Run the backend on a Mac/);
    assert.match(result.stdout, /– skip\s+xcodebuild: Skipped in demo mode\./);
    assert.match(result.stdout, /– skip\s+Xcode command line tools/);
    assert.match(result.stdout, /✔ ok\s+Data directory/);
    assert.match(result.stdout, /Overall: degraded \(1 warning, 4 skipped\)/);
    assert.doesNotMatch(result.stdout, /✔[^\n]*(Xcode|xcodebuild|Hypervisor|vphone|HEALTHY)/i, "no invented passes");
    assert.doesNotMatch(result.stdout, /HEALTHY/);

    const json = parse<{ status: string; checks: Array<{ id: string; status: string }> }>(await runCli(["doctor", "--json"], { url: backend.url }));
    assert.equal(json.status, "degraded");
    assert.equal(json.checks.find((c) => c.id === "xcodebuild")?.status, "skip");
  });
});

/** A stand-in backend that answers GET /doctor with whatever the test needs. */
async function fakeDoctor(report: unknown): Promise<{ url: string; close(): Promise<void> }> {
  const server = http.createServer((_req, res) => {
    res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify(report));
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  return {
    url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`,
    close: () =>
      new Promise<void>((resolve) => {
        server.close(() => resolve());
        server.closeAllConnections();
      })
  };
}

test("doctor exits 1 when the backend reports a failed check, and shows its message and remedy", async () => {
  const backend = await fakeDoctor({
    status: "unhealthy",
    mode: "live",
    generatedAt: new Date().toISOString(),
    checks: [
      { id: "xcodebuild", name: "xcodebuild", status: "fail", message: "xcodebuild -version failed: license not accepted", remedy: "Run: sudo xcodebuild -license accept" },
      { id: "data-dir", name: "Data directory", status: "ok", message: "/data (200 GB free)" }
    ]
  });
  try {
    const result = await runCli(["doctor"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stdout, /✖ fail\s+xcodebuild: xcodebuild -version failed: license not accepted/);
    assert.match(result.stdout, /Fix: Run: sudo xcodebuild -license accept/);
    assert.match(result.stdout, /✔ ok\s+Data directory: \/data \(200 GB free\)/);
    assert.match(result.stdout, /Overall: unhealthy \(1 failure\)/);
    assert.equal(count(result.stdout, /✔/), 1, "only the check the backend passed");
    assert.equal((await runCli(["doctor", "--json"], { url: backend.url })).code, 1, "same exit code with --json");
  } finally {
    await backend.close();
  }
});

test("doctor against a healthy live backend exits 0", async () => {
  const backend = await fakeDoctor({ status: "healthy", mode: "live", generatedAt: new Date().toISOString(), checks: [{ id: "mode", name: "Execution mode", status: "ok", message: "Live: driving real Xcode simulators." }] });
  try {
    const result = await runCli(["doctor"], { url: backend.url });
    assert.equal(result.code, 0);
    assert.match(result.stdout, /Overall: healthy/);
  } finally {
    await backend.close();
  }
});

test("catalog lists what the backend can create, and --json is the raw catalog", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["catalog"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /Runtimes \(3\)/);
    assert.match(result.stdout, /iOS 18\.0\s+18\.0\s+com\.apple\.CoreSimulator\.SimRuntime\.iOS-18-0/);
    assert.match(result.stdout, /Device types \(4\)/);
    assert.match(result.stdout, /iPhone 15\s+iPhone\s+com\.apple\.CoreSimulator\.SimDeviceType\.iPhone-15/);
    assert.match(result.stdout, /iPad \(10th generation\)\s+iPad/);
    const json = parse<{ runtimes: unknown[]; deviceTypes: unknown[]; source: string }>(await runCli(["catalog", "--json"], { url: backend.url }));
    assert.equal(json.runtimes.length, 3);
    assert.equal(json.source, "demo");
  });
});

// ---------------------------------------------------------------------------- devices

test("spawn, devices, boot, shutdown, rm: the whole device lifecycle, by id prefix or name", async () => {
  await withBackend(async (backend) => {
    const spawned = await runCli(["spawn", "Lifecycle Phone", "--runtime", "17.5", "--model", "iPhone SE (3rd generation)"], { url: backend.url });
    assert.equal(spawned.code, 0, spawned.stderr);
    assert.match(spawned.stdout, /Created Lifecycle Phone \(iOS 17\.5\)\s+id [0-9a-f]{8}\s+ready/);
    const [device] = backend.services.orchestrator.listDevices();
    assert.equal(device.runtime, RT_17);
    assert.equal(device.modelName, "iPhone SE (3rd generation)");

    const listed = await runCli(["devices"], { url: backend.url });
    assert.match(listed.stdout, new RegExp(`${device.id.slice(0, 8)}\\s+Lifecycle Phone\\s+iOS 17\\.5\\s+ready\\s+simctl\\s+no`));
    assert.equal((await runCli(["ls"], { url: backend.url })).stdout, listed.stdout, "`ls` is an alias");

    const byName = await runCli(["shutdown", "Lifecycle Phone"], { url: backend.url });
    assert.equal(byName.code, 0, byName.stderr);
    assert.match(byName.stdout, /Lifecycle Phone \(iOS 17\.5\) is stopped/);
    assert.equal(backend.services.orchestrator.getDevice(device.id)!.status, "stopped");

    const byPrefix = await runCli(["boot", device.id.slice(0, 6)], { url: backend.url });
    assert.equal(byPrefix.code, 0, byPrefix.stderr);
    assert.match(byPrefix.stdout, /is ready/);
    assert.equal(backend.services.orchestrator.getDevice(device.id)!.status, "ready");

    const byFullId = await runCli(["shutdown", device.id], { url: backend.url });
    assert.equal(byFullId.code, 0);

    const removed = await runCli(["rm", device.id.slice(0, 8)], { url: backend.url });
    assert.equal(removed.code, 0, removed.stderr);
    assert.match(removed.stdout, /Removed Lifecycle Phone \(iOS 17\.5\)/);
    assert.equal(backend.services.orchestrator.listDevices().length, 0);
    assert.match((await runCli(["devices"], { url: backend.url })).stdout, /No devices\./);
  });
});

test("legacy spawn <name> without options uses the backend's own defaults (nothing is hard-coded)", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["spawn", "iPhone 15"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    const [device] = backend.services.orchestrator.listDevices();
    assert.equal(device.runtime, "com.apple.CoreSimulator.SimRuntime.iOS-18-2", "the newest runtime the backend has, not iOS-18-0");
    assert.equal(device.name, "iPhone 15");
  });
});

test("spawn --json prints the device; --no-wait returns while it is still booting", async () => {
  await withBackend(
    async (backend) => {
      const json = parse<{ id: string; status: string; type: string }>(await runCli(["spawn", "J", "--json"], { url: backend.url }));
      assert.equal(json.status, "ready");
      assert.equal(json.type, "simulator");

      const early = await runCli(["spawn", "Early", "--no-wait"], { url: backend.url });
      assert.equal(early.code, 0);
      assert.match(early.stdout, /Creating Early \(iOS 18\.2\)\s+id [0-9a-f]{8}\s+booting\. Check on it with "ioslab devices"/);
    },
    { mockLatencyMs: 50 }
  );
});

test("spawn --vm creates a simulated VM and says the VM backend is experimental", async () => {
  await withBackend(async (backend) => {
    const result = await runCli(["spawn", "My VM", "--vm"], { url: backend.url });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /Note: the VM backend is experimental and simulated/);
    const [device] = backend.services.orchestrator.listDevices();
    assert.equal(device.type, "vm");
    assert.equal(device.backend, "simulated");
    assert.match((await runCli(["devices"], { url: backend.url })).stdout, /My VM\s+\S+\s+ready\s+simulated/);
  });
});

test("device references: a name shared by several devices is ambiguous, candidates are listed, exit 1", async () => {
  await withBackend(async (backend) => {
    await runCli(["spawn", "Twin"], { url: backend.url });
    await runCli(["spawn", "Twin"], { url: backend.url });
    const [a, b] = backend.services.orchestrator.listDevices();

    for (const command of ["boot", "shutdown", "rm"]) {
      const result = await runCli([command, "Twin"], { url: backend.url });
      assert.equal(result.code, 1, command);
      assert.match(result.stderr, /"Twin" matches 2 devices\. Use a longer id prefix to pick one:/);
      assert.ok(result.stderr.includes(a.id.slice(0, 8)) && result.stderr.includes(b.id.slice(0, 8)), "both candidates are listed");
      assert.match(result.stderr, /iOS 18\.2\s+ready/);
    }
    assert.equal(backend.services.orchestrator.listDevices().length, 2, "nothing was touched");

    const unambiguous = await runCli(["shutdown", a.id.slice(0, 8)], { url: backend.url });
    assert.equal(unambiguous.code, 0);
    assert.equal(backend.services.orchestrator.getDevice(a.id)!.status, "stopped");
    assert.equal(backend.services.orchestrator.getDevice(b.id)!.status, "ready");
  });
});

test("device commands with an unknown device: the server's message, exit 1", async () => {
  await withBackend(async (backend) => {
    for (const command of ["boot", "shutdown", "rm"]) {
      const result = await runCli([command, "does-not-exist"], { url: backend.url });
      assert.equal(result.code, 1, command);
      assert.match(result.stderr, /Device not found: does-not-exist/);
      assert.match(result.stderr, /ioslab devices/);
    }
  });
});

test("a device operation the backend refuses is exit 1 with the backend's reason (capacity)", async () => {
  await withBackend(async (backend) => {
    for (let i = 0; i < 4; i += 1) assert.equal((await runCli(["spawn", `Sim ${i}`], { url: backend.url })).code, 0);
    const result = await runCli(["spawn", "One too many"], { url: backend.url });
    assert.equal(result.code, 1);
    assert.match(result.stderr, /Capacity exceeded: 4 of 4 units in use/);
  });
});

test("spawn with an unknown runtime or model prints the server's list of what exists", async () => {
  await withBackend(async (backend) => {
    const runtime = await runCli(["spawn", "--runtime", "1.0"], { url: backend.url });
    assert.equal(runtime.code, 1);
    assert.match(runtime.stderr, /Unknown runtime "1\.0"\. Available: iOS 18\.2, iOS 18\.0, iOS 17\.5/);
    const model = await runCli(["spawn", "--model", "Nokia 3310"], { url: backend.url });
    assert.equal(model.code, 1);
    assert.match(model.stderr, /Unknown device type "Nokia 3310"\. Available: iPhone SE/);
  });
});

// ---------------------------------------------------------------------------- VMs

test("vm new / list / boot / backup / restore / switch work against the current API and say they are simulated", async () => {
  await withBackend(async (backend) => {
    const created = await runCli(["vm", "new", "Test VM", "--cpu", "2", "--memory", "3", "--disk", "40"], { url: backend.url });
    assert.equal(created.code, 0, created.stderr);
    assert.match(created.stdout, /Note: the VM backend is experimental and simulated/);
    assert.match(created.stdout, /Created VM Test VM/);
    const [vm] = backend.services.orchestrator.listDevices();
    assert.deepEqual([vm.cpu, vm.memory, vm.disk], [2, 3, 40]);

    const list = await runCli(["vm", "list"], { url: backend.url });
    assert.match(list.stdout, /experimental and simulated/);
    assert.match(list.stdout, /ID\s+NAME\s+STATUS\s+CPU\s+MEMORY\s+DISK\s+BACKUPS/);
    assert.match(list.stdout, new RegExp(`${vm.id.slice(0, 8)}\\s+Test VM\\s+ready\\s+2\\s+3 GB\\s+40 GB\\s+Clean Install`));

    assert.equal((await runCli(["vm", "backup", vm.id.slice(0, 8), "Snap A"], { url: backend.url })).code, 0);
    assert.deepEqual(backend.services.orchestrator.getDevice(vm.id)!.backupList, ["Clean Install", "Snap A"]);
    const restored = await runCli(["vm", "restore", "Test VM", "Snap A"], { url: backend.url });
    assert.equal(restored.code, 0, restored.stderr);
    assert.match(restored.stdout, /Restored VM Test VM to backup "Snap A"/);

    const switched = await runCli(["vm", "switch", vm.id.slice(0, 8), "--cpu", "8", "--memory", "16"], { url: backend.url });
    assert.equal(switched.code, 0, switched.stderr);
    assert.match(switched.stdout, /now has 8 vCPU, 16 GB RAM, 40 GB disk/);

    assert.equal((await runCli(["vm", "boot", "Test VM"], { url: backend.url })).code, 0);

    const json = await runCli(["vm", "list", "--json"], { url: backend.url });
    assert.equal(parse<{ simulated: boolean }>(json).simulated, true, "stdout is only the JSON");
    assert.match(json.stderr, /experimental and simulated/);
  });
});

test("vm errors from the backend are exit 1 with its message; switch without options is a usage error", async () => {
  await withBackend(async (backend) => {
    await runCli(["vm", "new", "Err VM"], { url: backend.url });
    const restore = await runCli(["vm", "restore", "Err VM", "No Such Backup"], { url: backend.url });
    assert.equal(restore.code, 1);
    assert.match(restore.stderr, /Backup "No Such Backup" not found for VM/);

    const missing = await runCli(["vm", "boot", "ghost"], { url: backend.url });
    assert.equal(missing.code, 1);
    assert.match(missing.stderr, /not found: ghost/);

    const none = await runCli(["vm", "switch", "Err VM"], { url: backend.url });
    assert.equal(none.code, 2);
    assert.match(none.stderr, /at least one of --cpu, --memory or --disk/);
  });
});

test("vm commands against a backend with the VM API off: exit 1 with the backend's explanation", async () => {
  await withBackend(
    async (backend) => {
      const result = await runCli(["vm", "list"], { url: backend.url });
      assert.equal(result.code, 1);
      assert.match(result.stderr, /experimental VM backend is disabled/);
    },
    { experimentalVm: false }
  );
});

// ---------------------------------------------------------------------------- connection, errors, config

test("an unreachable backend is exit 2 with one clear line, for every command", async () => {
  const url = await closedPortUrl();
  const expected = `Cannot reach the MobileLab backend at ${url}. Start it with "make dev" or set IOSLAB_API_URL.`;
  const commands = [
    ["status"],
    ["doctor"],
    ["catalog"],
    ["devices"],
    ["spawn", "X"],
    ["boot", "x"],
    ["shutdown", "x"],
    ["rm", "x"],
    ["test", "run", "AppTests"],
    ["test", "run", "AppTests", "--no-wait"],
    ["test", "list"],
    ["test", "show", "x"],
    ["test", "cancel", "x"],
    ["test", "rerun", "x"],
    ["test", "junit", "x"],
    ["logs", "x"],
    ["logs", "-f", "x"],
    ["vm", "list"],
    ["vm", "new", "VM"]
  ];
  for (const args of commands) {
    const result = await runCli(args, { url });
    assert.equal(result.code, 2, args.join(" "));
    assert.equal(result.stderr.trim(), expected, args.join(" "));
    assert.equal(result.stdout, "", `${args.join(" ")} prints nothing on stdout`);
  }
});

test("--api overrides IOSLAB_API_URL, and the default is http://127.0.0.1:4000", async () => {
  await withBackend(async (backend) => {
    const closed = await closedPortUrl();
    const viaFlag = await runCli(["--api", backend.url, "devices"], { url: closed });
    assert.equal(viaFlag.code, 0, viaFlag.stderr);
    const afterCommand = await runCli(["devices", "--api", backend.url], { url: closed });
    assert.equal(afterCommand.code, 0, "global options work after the command as well");
    const bare = await runCli(["--api", backend.url.replace("http://", ""), "devices"]);
    assert.equal(bare.code, 0, "a bare host:port is accepted");
    assert.equal((await runCli(["--api", "ftp://x", "devices"])).code, 2);
  });
  const noEnv = await runCli(["devices"], { env: { IOSLAB_API_URL: "" } });
  assert.match(noEnv.stderr, /Cannot reach the MobileLab backend at http:\/\/127\.0\.0\.1:4000\./);
});

test("usage errors are exit 2 and never touch the backend", async () => {
  const url = await closedPortUrl();
  for (const args of [
    [],
    ["nonsense"],
    ["test"],
    ["test", "run"],
    ["test", "run", "X", "--parallel", "0"],
    ["test", "run", "X", "--retries", "many"],
    ["test", "run", "X", "--timeout", "-3"],
    ["test", "list", "--status", "sleeping"],
    ["test", "list", "--limit", "0"],
    ["boot"],
    ["logs"],
    ["vm", "backup", "only-one-arg"],
    ["--bogus"]
  ]) {
    const result = await runCli(args, { url });
    assert.equal(result.code, 2, args.join(" "));
    assert.doesNotMatch(result.stderr, /Cannot reach/, args.join(" "));
    assert.notEqual(result.stderr, "", `${args.join(" ")} explains what is wrong`);
  }
});

test("--help and --version are exit 0; the top-level help documents the commands, examples, env and exit codes", async () => {
  const help = await runCli(["--help"]);
  assert.equal(help.code, 0);
  for (const word of ["status", "doctor", "catalog", "devices", "spawn", "boot", "shutdown", "rm", "test", "logs", "vm", "--api", "--token", "--json", "--no-color", "Examples:", "IOSLAB_API_URL", "IOSLAB_API_TOKEN", "NO_COLOR", "Exit codes:"]) {
    assert.ok(help.stdout.includes(word), `--help mentions ${word}`);
  }
  const run = await runCli(["test", "run", "--help"]);
  assert.equal(run.code, 0);
  for (const flag of ["--project", "--workspace", "--dir", "--configuration", "--only-testing", "--runtime", "--model", "--parallel", "--retries", "--no-provision", "--no-wait", "--timeout", "--junit", "--verbose"]) {
    assert.ok(run.stdout.includes(flag), `test run --help mentions ${flag}`);
  }
  assert.match((await runCli(["--version"])).stdout, /^\d+\.\d+\.\d+\n$/);
  assert.equal((await runCli(["help", "logs"])).code, 0);
});

test("every command and option has a description (help is genuinely useful)", async () => {
  const seen: string[] = [];
  const walk = async (words: string[]) => {
    const result = await runCli([...words, "--help"]);
    assert.equal(result.code, 0, words.join(" "));
    // every line in a Commands: or Options: block has a description column
    let block: "commands" | "options" | "arguments" | undefined;
    for (const line of result.stdout.split("\n")) {
      if (/^(Commands|Options|Global Options|Arguments):$/.test(line)) {
        block = line.startsWith("Commands") ? "commands" : "options";
        continue;
      }
      if (line.trim() === "") {
        block = undefined;
        continue;
      }
      if (block && /^ {2}\S/.test(line)) {
        assert.match(line, /^ {2}\S.*? {2,}\S/, `"${line.trim()}" in \`ioslab ${words.join(" ")} --help\` has no description`);
        if (block === "commands") {
          const name = line.trim().split(/[\s|]/)[0];
          if (name !== "help") seen.push([...words, name].join(" "));
        }
      }
    }
  };
  await walk([]);
  for (const command of [...seen]) if (["test", "vm"].includes(command)) await walk([command]);
  for (const command of seen.filter((c) => !["test", "vm"].includes(c))) await walk(command.split(" "));
  assert.ok(seen.length >= 20, `found ${seen.length} commands`);
});

// ---------------------------------------------------------------------------- authentication

test("token auth: no token or a wrong token is exit 1 with the 401 message; the right one works, everywhere", async () => {
  await withBackend(
    async (backend) => {
      const anonymous = await runCli(["devices"], { url: backend.url });
      assert.equal(anonymous.code, 1);
      assert.match(anonymous.stderr, /Unauthorized \(HTTP 401\): Missing or invalid API token/);
      assert.match(anonymous.stderr, /Set IOSLAB_API_TOKEN or pass --token/);

      const wrongFlag = await runCli(["devices", "--token", "wrong"], { url: backend.url });
      assert.equal(wrongFlag.code, 1);
      assert.match(wrongFlag.stderr, /HTTP 401/);
      const wrongEnv = await runCli(["status"], { url: backend.url, env: { IOSLAB_API_TOKEN: "wrong" } });
      assert.equal(wrongEnv.code, 1);
      assert.match(wrongEnv.stderr, /HTTP 401/);

      assert.equal((await runCli(["devices", "--token", "s3cret"], { url: backend.url })).code, 0);
      assert.equal((await runCli(["--token", "s3cret", "devices"], { url: backend.url })).code, 0);
      assert.equal((await runCli(["status"], { url: backend.url, env: { IOSLAB_API_TOKEN: "s3cret" } })).code, 0);
      assert.equal((await runCli(["--token", "s3cret", "devices"], { url: backend.url, env: { IOSLAB_API_TOKEN: "wrong" } })).code, 0, "the flag beats the environment");

      // Running tests needs the token on the WebSocket handshake too: the run-level line only comes from the socket.
      const env = { IOSLAB_API_TOKEN: "s3cret" };
      const run = await runCli(["test", "run", "AppTests", "--runtime", "18.0", "--runtime", "17.5"], { url: backend.url, env });
      assert.equal(run.code, 0, run.stdout + run.stderr);
      assert.match(run.stdout, /Run of AppTests: 2 jobs/, "the authenticated WebSocket worked");

      const [job] = backend.services.orchestrator.listJobs();
      assert.equal((await runCli(["logs", "-f", job.id.slice(0, 8)], { url: backend.url, env })).code, 0);
      assert.equal((await runCli(["test", "run", "AppTests"], { url: backend.url })).code, 1, "test run without a token");
    },
    { apiToken: "s3cret" }
  );
});

test("/health is public, so a wrong token still reaches the demo-mode check without leaking anything", async () => {
  await withBackend(
    async (backend) => {
      const result = await runCli(["test", "run", "AppTests", "--token", "nope"], { url: backend.url });
      assert.equal(result.code, 1);
      assert.match(result.stderr, /DEMO MODE/);
      assert.match(result.stderr, /HTTP 401/);
      assert.equal(result.stdout, "");
    },
    { apiToken: "s3cret" }
  );
});

// ---------------------------------------------------------------------------- output

test("colors: on for a terminal, off for pipes, NO_COLOR and --no-color", async () => {
  await withBackend(async (backend) => {
    await runCli(["spawn", "Color Phone"], { url: backend.url });
    const terminal = await runCli(["devices"], { url: backend.url, tty: true });
    assert.match(terminal.stdout, ANSI);
    for (const options of [{ tty: false }, { tty: true, env: { NO_COLOR: "1" } }]) {
      const plain = await runCli(["devices"], { url: backend.url, ...options });
      assert.doesNotMatch(plain.stdout, ANSI, JSON.stringify(options));
      assert.match(plain.stdout, /Color Phone/);
    }
    const flagged = await runCli(["--no-color", "devices"], { url: backend.url, tty: true });
    assert.doesNotMatch(flagged.stdout, ANSI);
    const run = await runCli(["test", "run", "FailApp"], { url: backend.url, tty: true, env: { NO_COLOR: "1" } });
    assert.doesNotMatch(run.stdout + run.stderr, ANSI, "test run output has no ANSI with NO_COLOR either");
    assert.doesNotMatch((await runCli(["status", "--json"], { url: backend.url, tty: true })).stdout, ANSI, "JSON is never colored");
  });
});

test("text that comes from the server cannot inject escape sequences into the terminal", async () => {
  await withBackend(async (backend) => {
    await runCli(["spawn", "Evil\u001b[2J\u001b]0;pwned\u0007 Phone"], { url: backend.url });
    const listed = await runCli(["devices"], { url: backend.url, tty: false });
    assert.doesNotMatch(listed.stdout, /\u001b/);
    assert.match(listed.stdout, /Evil/);
  });
});

test("a bad API token is a usage error before any request is made; surrounding whitespace is ignored", async () => {
  const bad = await runCli(["devices", "--token", "two words"], { url: await closedPortUrl() });
  assert.equal(bad.code, 2);
  assert.match(bad.stderr, /API token contains characters that cannot be sent/);
  assert.doesNotMatch(bad.stderr, /Cannot reach/);

  await withBackend(
    async (backend) => {
      const padded = await runCli(["devices"], { url: backend.url, env: { IOSLAB_API_TOKEN: "s3cret\n" } });
      assert.equal(padded.code, 0, padded.stderr);
    },
    { apiToken: "s3cret" }
  );
});

test("Ctrl+C during a command that has no cleanup to do just stops it (exit 130)", async () => {
  await withBackend(
    async (backend) => {
      const controller = new AbortController();
      const spawning = runCli(["spawn", "Slow Phone"], { url: backend.url, interrupt: controller.signal });
      await new Promise((resolve) => setTimeout(resolve, 30));
      controller.abort(130);
      const result = await spawning;
      assert.equal(result.code, 130);
      assert.match(result.stderr, /Interrupted\./);

      // SIGTERM (what CI systems send) is 143
      const term = new AbortController();
      const again = runCli(["spawn", "Other Phone"], { url: backend.url, interrupt: term.signal });
      await new Promise((resolve) => setTimeout(resolve, 30));
      term.abort(143);
      assert.equal((await again).code, 143);
    },
    { mockLatencyMs: 300 }
  );
});

test("main() takes plain function writers and tolerates a raw process.argv", async () => {
  await withBackend(async (backend) => {
    let out = "";
    let err = "";
    const code = await main(["node", "/usr/local/bin/ioslab", "devices"], {
      stdout: (text) => void (out += text),
      stderr: (text) => void (err += text),
      env: { IOSLAB_API_URL: backend.url }
    });
    assert.equal(code, 0);
    assert.match(out, /No devices\./);
    assert.equal(err, "");
  });
});
