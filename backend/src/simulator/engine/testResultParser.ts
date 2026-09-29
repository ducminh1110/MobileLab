export type TestCaseStatus = "passed" | "failed" | "skipped";

export interface TestCaseResult {
  className: string;
  name: string;
  status: TestCaseStatus;
  durationSeconds: number;
  /** Assertion / issue text, "file:line message", when the test failed. */
  message?: string;
}

export interface TestSummary {
  total: number;
  passed: number;
  failed: number;
  skipped: number;
  durationSeconds: number;
  /** xcodebuild never reached the test phase (compile error, missing scheme, ...). */
  buildFailed: boolean;
  /** First few compiler / xcodebuild errors, for a readable failure reason. */
  errors: string[];
}

export interface ParsedTestRun {
  cases: TestCaseResult[];
  summary: TestSummary;
}

// eslint-disable-next-line no-control-regex
const ANSI = /\u001b\[[0-9;]*m/g;

// Test Case '-[Module.Class method]' passed (0.001 seconds).
// Test case 'Class.method()' passed on 'iPhone 15 - App (1234)' (0.001 seconds)
const XCTEST_RESULT = /^Test [Cc]ase '(.+)' (passed|failed|skipped)(?: on '.*?')? \(([\d.]+) seconds\)\.?$/;
// <symbol> Test "Name" passed after 0.001 seconds.   (the symbol is an SF Symbols glyph in xcodebuild output)
const SWIFT_TESTING_RESULT = /^[^\w\s]\s+Test (.+?) (passed|failed) after ([\d.]+) seconds/u;
const SWIFT_TESTING_ISSUE = /^[^\w\s]\s+Test (.+?) recorded an issue(?: at (\S+?))?: (.*)$/u;
const SWIFT_TESTING_SKIP = /^[^\w\s]\s+Test (.+?) skipped/u;
// /path/File.swift:42: error: -[Module.Class method] : XCTAssertEqual failed: ...
const XCTEST_FAILURE = /^(.+?):(\d+): error: (-\[\S+ \S+\]|[\w.]+\(\)) : (.*)$/;
const COMPILER_ERROR = /^(.+?):(\d+):(\d+): error: (.+)$/;
const TOOL_ERROR = /^(?:xcodebuild: )?error: (.+)$/;
const BUILD_FAILED = /^\*\* (?:TEST )?BUILD FAILED \*\*/;
const TEST_FAILED = /^\*\* TEST FAILED \*\*/;

function splitCaseId(raw: string): { className: string; name: string } {
  const objc = /^-\[(\S+) (\S+)\]$/.exec(raw);
  if (objc) return { className: objc[1], name: objc[2] };

  const withoutQuotes = raw.replace(/^"(.*)"$/, "$1");
  const paren = withoutQuotes.indexOf("(");
  const head = paren === -1 ? withoutQuotes : withoutQuotes.slice(0, paren);
  const dot = head.lastIndexOf(".");
  if (dot > 0 && !withoutQuotes.includes(" ")) {
    return { className: withoutQuotes.slice(0, dot), name: withoutQuotes.slice(dot + 1) };
  }
  return { className: "", name: withoutQuotes };
}

function caseKey(className: string, name: string): string {
  return `${className}/${name}`;
}

/**
 * Incremental parser for `xcodebuild test` output. Feed it lines as they stream in, then call
 * `finish()`. It understands classic XCTest output (both the `-[Module.Class method]` and the newer
 * `Class.method()` spellings) and Swift Testing.
 */
export class TestResultParser {
  private readonly cases = new Map<string, TestCaseResult>();
  private readonly pendingFailures = new Map<string, string[]>();
  private readonly errors: string[] = [];
  private buildFailed = false;
  private testsFailed = false;

  push(rawLine: string): void {
    const line = rawLine.replace(ANSI, "").trim();
    if (!line) return;

    let match = XCTEST_RESULT.exec(line);
    if (match) {
      const { className, name } = splitCaseId(match[1]);
      this.record(className, name, match[2] as TestCaseStatus, Number(match[3]));
      return;
    }

    match = SWIFT_TESTING_RESULT.exec(line);
    if (match) {
      const { className, name } = splitCaseId(match[1]);
      this.record(className, name, match[2] as TestCaseStatus, Number(match[3]));
      return;
    }

    match = SWIFT_TESTING_SKIP.exec(line);
    if (match) {
      const { className, name } = splitCaseId(match[1]);
      this.record(className, name, "skipped", 0);
      return;
    }

    match = SWIFT_TESTING_ISSUE.exec(line);
    if (match) {
      const { className, name } = splitCaseId(match[1]);
      this.addFailure(caseKey(className, name), match[2] ? `${match[2]} ${match[3]}` : match[3]);
      return;
    }

    match = XCTEST_FAILURE.exec(line);
    if (match) {
      const { className, name } = splitCaseId(match[3]);
      this.addFailure(caseKey(className, name), `${match[1].split("/").pop()}:${match[2]} ${match[4]}`);
      return;
    }

    if (BUILD_FAILED.test(line)) {
      this.buildFailed = true;
      return;
    }
    if (TEST_FAILED.test(line)) {
      this.testsFailed = true;
      return;
    }

    match = COMPILER_ERROR.exec(line) ?? TOOL_ERROR.exec(line);
    if (match) {
      const text = match.length === 5 ? `${match[1].split("/").pop()}:${match[2]}: ${match[4]}` : match[1];
      if (this.errors.length < 20 && !this.errors.includes(text)) this.errors.push(text);
    }
  }

  finish(): ParsedTestRun {
    const cases = [...this.cases.values()];
    const passed = cases.filter((c) => c.status === "passed").length;
    const failed = cases.filter((c) => c.status === "failed").length;
    const skipped = cases.filter((c) => c.status === "skipped").length;
    const buildFailed = this.buildFailed || (cases.length === 0 && !this.testsFailed && this.errors.length > 0);

    return {
      cases,
      summary: {
        total: cases.length,
        passed,
        failed,
        skipped,
        durationSeconds: Math.round(cases.reduce((sum, c) => sum + c.durationSeconds, 0) * 1000) / 1000,
        buildFailed,
        errors: this.errors.slice(0, 5)
      }
    };
  }

  private addFailure(key: string, message: string): void {
    const list = this.pendingFailures.get(key) ?? [];
    list.push(message);
    this.pendingFailures.set(key, list);
    // Swift Testing prints issues after the test started but the result line may already exist.
    const existing = this.cases.get(key);
    if (existing && existing.status === "failed") existing.message = list.join("\n");
  }

  private record(className: string, name: string, status: TestCaseStatus, durationSeconds: number): void {
    const key = caseKey(className, name);
    const messages = this.pendingFailures.get(key);
    // Re-runs of the same test (retry-on-failure, clones) replace the earlier result: the last word wins.
    this.cases.set(key, {
      className,
      name,
      status,
      durationSeconds,
      message: status === "failed" && messages ? messages.join("\n") : undefined
    });
    if (status !== "failed") this.pendingFailures.delete(key);
  }
}

export function parseTestOutput(text: string): ParsedTestRun {
  const parser = new TestResultParser();
  for (const line of text.split("\n")) parser.push(line);
  return parser.finish();
}

function escapeXml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    // eslint-disable-next-line no-control-regex
    .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, "");
}

/** Renders parsed results as JUnit XML, which every CI system can ingest. */
export function toJUnitXml(suiteName: string, run: ParsedTestRun, timestamp: string): string {
  const { summary, cases } = run;
  const lines: string[] = ['<?xml version="1.0" encoding="UTF-8"?>'];
  lines.push(
    `<testsuites tests="${summary.total}" failures="${summary.failed}" skipped="${summary.skipped}" time="${summary.durationSeconds}">`
  );
  lines.push(
    `  <testsuite name="${escapeXml(suiteName)}" tests="${summary.total}" failures="${summary.failed}" skipped="${summary.skipped}" time="${summary.durationSeconds}" timestamp="${escapeXml(timestamp)}">`
  );

  if (summary.buildFailed) {
    const message = summary.errors[0] ?? "Build failed";
    lines.push(`    <testcase name="build" classname="${escapeXml(suiteName)}" time="0">`);
    lines.push(`      <error message="${escapeXml(message)}">${escapeXml(summary.errors.join("\n"))}</error>`);
    lines.push("    </testcase>");
  }

  for (const c of cases) {
    const attrs = `name="${escapeXml(c.name)}" classname="${escapeXml(c.className || suiteName)}" time="${c.durationSeconds}"`;
    if (c.status === "passed") {
      lines.push(`    <testcase ${attrs}/>`);
    } else if (c.status === "skipped") {
      lines.push(`    <testcase ${attrs}><skipped/></testcase>`);
    } else {
      const message = c.message ?? "Test failed";
      lines.push(`    <testcase ${attrs}>`);
      lines.push(`      <failure message="${escapeXml(message.split("\n")[0])}">${escapeXml(message)}</failure>`);
      lines.push("    </testcase>");
    }
  }

  lines.push("  </testsuite>");
  lines.push("</testsuites>");
  return lines.join("\n");
}
