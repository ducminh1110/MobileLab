import test from "node:test";
import assert from "node:assert/strict";
import { parseTestOutput, toJUnitXml } from "../../src/simulator/engine/testResultParser";

const CLASSIC = `
Test Suite 'All tests' started at 2025-01-01 10:00:00.000
Test Case '-[AppTests.LoginTests testValid]' started.
Test Case '-[AppTests.LoginTests testValid]' passed (0.012 seconds).
Test Case '-[AppTests.LoginTests testInvalid]' started.
/Users/dev/App/LoginTests.swift:42: error: -[AppTests.LoginTests testInvalid] : XCTAssertEqual failed: ("a") is not equal to ("b")
Test Case '-[AppTests.LoginTests testInvalid]' failed (0.034 seconds).
Test Case '-[AppTests.LoginTests testSkipped]' skipped (0.001 seconds).
	 Executed 3 tests, with 1 failure (0 unexpected) in 0.05 (0.06) seconds
** TEST FAILED **
`;

test("parses classic XCTest output including failure messages", () => {
  const { cases, summary } = parseTestOutput(CLASSIC);
  assert.equal(summary.total, 3);
  assert.equal(summary.passed, 1);
  assert.equal(summary.failed, 1);
  assert.equal(summary.skipped, 1);
  assert.equal(summary.buildFailed, false);

  const failed = cases.find((c) => c.status === "failed")!;
  assert.equal(failed.className, "AppTests.LoginTests");
  assert.equal(failed.name, "testInvalid");
  assert.match(failed.message!, /LoginTests\.swift:42 XCTAssertEqual failed/);
  assert.equal(failed.durationSeconds, 0.034);
});

test("parses the newer Class.method() spelling and 'on device' suffix", () => {
  const out = `
Test case 'LoginTests.testValid()' passed on 'iPhone 15 - App (1234)' (0.234 seconds)
/x/LoginTests.swift:9: error: LoginTests.testBad() : XCTAssertTrue failed
Test case 'LoginTests.testBad()' failed on 'iPhone 15 - App (1234)' (0.100 seconds)
`;
  const { cases, summary } = parseTestOutput(out);
  assert.equal(summary.total, 2);
  assert.deepEqual(
    cases.map((c) => [c.className, c.name, c.status]),
    [
      ["LoginTests", "testValid()", "passed"],
      ["LoginTests", "testBad()", "failed"]
    ]
  );
  assert.match(cases[1].message!, /XCTAssertTrue failed/);
});

test("parses Swift Testing output (the leading symbol glyph is ignored)", () => {
  const out = [
    '\u{100888} Test "Login works" started.',
    '\u{100888} Test "Login works" passed after 0.002 seconds.',
    "✘ Test rejectsBadInput() recorded an issue at Auth.swift:12:5: Expectation failed: (1) == (2)",
    "✘ Test rejectsBadInput() failed after 0.010 seconds with 1 issue."
  ].join("\n");
  const { cases, summary } = parseTestOutput(out);
  assert.equal(summary.total, 2);
  assert.equal(cases[0].name, "Login works");
  assert.equal(cases[1].status, "failed");
  assert.match(cases[1].message!, /Expectation failed/);
});

test("a rerun of the same test replaces the earlier result", () => {
  const out = `
Test Case '-[A.B c]' failed (0.1 seconds).
Test Case '-[A.B c]' passed (0.1 seconds).
`;
  const { cases, summary } = parseTestOutput(out);
  assert.equal(cases.length, 1);
  assert.equal(summary.passed, 1);
  assert.equal(summary.failed, 0);
});

test("reports a build failure and its errors when no tests ran", () => {
  const out = `
/Users/dev/App/Foo.swift:10:5: error: cannot find 'bar' in scope
xcodebuild: error: something else
** TEST BUILD FAILED **
`;
  const { summary } = parseTestOutput(out);
  assert.equal(summary.total, 0);
  assert.equal(summary.buildFailed, true);
  assert.equal(summary.errors[0], "Foo.swift:10: cannot find 'bar' in scope");
  assert.equal(summary.errors[1], "something else");
});

test("a missing scheme is a build failure, not zero passing tests", () => {
  const { summary } = parseTestOutput('xcodebuild: error: The project named "Demo" does not contain a scheme named "Nope".');
  assert.equal(summary.buildFailed, true);
});

test("strips ANSI colour codes", () => {
  const { summary } = parseTestOutput("\u001b[32mTest Case '-[A.B c]' passed (0.1 seconds).\u001b[0m");
  assert.equal(summary.passed, 1);
});

test("renders valid, escaped JUnit XML", () => {
  const run = parseTestOutput(CLASSIC.replace("XCTAssertEqual", 'XCTAssert <&">'));
  const xml = toJUnitXml("App & Co", run, "2025-01-01T00:00:00Z");
  assert.match(xml, /^<\?xml version="1.0"/);
  assert.match(xml, /<testsuites tests="3" failures="1" skipped="1"/);
  assert.match(xml, /name="App &amp; Co"/);
  assert.match(xml, /<failure message="LoginTests\.swift:42 XCTAssert &lt;&amp;&quot;&gt; failed/);
  assert.match(xml, /<skipped\/>/);
  assert.ok(!xml.includes("<&"), "unescaped markup leaked into the XML");
});
