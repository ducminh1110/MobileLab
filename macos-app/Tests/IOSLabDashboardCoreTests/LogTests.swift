import XCTest
@testable import IOSLabDashboardCore

final class LogTests: XCTestCase {
    private func info(_ line: String) -> LogLineInfo { LogClassifier.classify(line) }

    func testClassifiesTheLinesXcodebuildWrites() {
        XCTAssertEqual(info("# MobileLab job abc, attempt 1, iPhone 15 (UDID)").kind, .header)
        XCTAssertEqual(info("Test Suite 'All tests' started at 2026-09-29 16:22:54 +0000").kind, .suiteStarted)
        XCTAssertEqual(info("Test Suite 'LoginTests' failed at 2026-09-29 16:22:55 +0000.").kind, .suiteFinished(passed: false))
        XCTAssertEqual(info("Test Suite 'LoginTests' passed at 2026-09-29 16:22:55 +0000.").kind, .suiteFinished(passed: true))
        XCTAssertEqual(info("Test Case '-[App.LoginTests testA]' started.").kind, .caseStarted)
        XCTAssertEqual(info("Test Case '-[App.LoginTests testA]' passed (0.020 seconds).").kind, .casePassed)
        XCTAssertEqual(info("Test Case '-[App.LoginTests testA]' failed (0.030 seconds).").kind, .caseFailed)
        XCTAssertEqual(info("Test Case '-[App.LoginTests testA]' skipped (0.000 seconds).").kind, .caseSkipped)
        XCTAssertEqual(info("Test case 'LoginTests.testA()' passed on 'iPhone 15 - App (1234)' (0.001 seconds)").kind, .casePassed)
        XCTAssertEqual(info("\t Executed 8 tests, with 2 failures (0 unexpected) in 0.412 (0.418) seconds").kind, .executed(failed: true))
        XCTAssertEqual(info("\t Executed 8 tests, with 0 failures (0 unexpected) in 0.412 (0.418) seconds").kind, .executed(failed: false))
        XCTAssertEqual(info("** TEST SUCCEEDED **").kind, .testSucceeded)
        XCTAssertEqual(info("** TEST FAILED **").kind, .testFailed)
        XCTAssertEqual(info("** BUILD FAILED **").kind, .buildFailed)
        XCTAssertEqual(info("Testing started on 'iPhone 15 Test'").kind, .plain)
        XCTAssertEqual(info("").kind, .plain)
    }

    func testGutterDiamondsMarkTestCaseResults() {
        XCTAssertEqual(info("Test Case '-[A.B c]' passed (0.1 seconds).").gutter, .passed)
        XCTAssertEqual(info("Test Case '-[A.B c]' failed (0.1 seconds).").gutter, .failed)
        XCTAssertEqual(info("Test Case '-[A.B c]' skipped (0.1 seconds).").gutter, .skipped)
        XCTAssertNil(info("Test Case '-[A.B c]' started.").gutter)
        XCTAssertNil(info("Test Suite 'X' failed at now").gutter)
    }

    func testFailureLinesCarryThePillText() {
        let line = "/Users/dev/Demo/LoginTests.swift:42: error: -[LoginFailTestsTests.LoginTests testInvalidPassword] : XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")"
        let result = info(line)
        XCTAssertEqual(result.kind, .failure)
        XCTAssertTrue(result.isErrorLine)
        XCTAssertEqual(result.pill, "XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")")
    }

    func testCompilerAndToolErrors() {
        let compiler = info("/Users/dev/App/LoginView.swift:12:5: error: cannot find 'foo' in scope")
        XCTAssertEqual(compiler.kind, .failure)
        XCTAssertEqual(compiler.pill, "cannot find 'foo' in scope")
        let tool = info("xcodebuild: error: Unable to find a destination matching the provided destination specifier")
        XCTAssertEqual(tool.kind, .error)
        XCTAssertEqual(tool.pill, "Unable to find a destination matching the provided destination specifier")
        XCTAssertEqual(info("error: The project named \"Demo\" does not contain a scheme").kind, .error)
        XCTAssertEqual(info("/Users/dev/App/A.swift:3:1: warning: unused variable").kind, .warning)
        XCTAssertFalse(info("/Users/dev/App/A.swift:3:1: warning: unused variable").isErrorLine)
    }

    func testSwiftTestingLines() {
        XCTAssertEqual(info("\u{2714} Test \"login works\" passed after 0.001 seconds.").kind, .casePassed)
        XCTAssertEqual(info("\u{2718} Test \"login works\" failed after 0.002 seconds with 1 issue.").kind, .caseFailed)
        XCTAssertEqual(info("\u{2718} Test \"login works\" recorded an issue at LoginTests.swift:9:5: Expectation failed: (a == b)").kind, .failure)
    }

    func testErrorLinesAreFlagged() {
        for kind in ["** TEST FAILED **", "** BUILD FAILED **"] { XCTAssertTrue(info(kind).isErrorLine) }
        XCTAssertFalse(info("Test Case '-[A.B c]' failed (0.1 seconds).").isErrorLine, "the failing test line gets a diamond, the assertion line above it gets the red background")
    }

    // MARK: Tokens

    func testTokensAlwaysReassembleTheLine() throws {
        var lines: [String] = []
        for name in ["output-fail", "output-pass", "output-missing", "output-running"] {
            lines += LogDocument.splitLines(try Fixture.decode(JobOutput.self, name).text)
        }
        lines += try Fixture.streamedEvents().filter(\.isOutput).flatMap { LogDocument.splitLines($0.message) }
        lines += ["", "  ", "'unterminated", "\"", "-[", "Test Case ''", "Test Case '-[]' passed (x seconds).", "\u{2714} Test \"é\" passed after 0.1 seconds.", "😀 emoji Test Case 'x'"]
        XCTAssertGreaterThan(lines.count, 60)
        for line in lines {
            let kind = LogClassifier.classify(line).kind
            let joined = LogClassifier.tokens(for: line, kind: kind).map(\.text).joined()
            XCTAssertEqual(joined, line, "tokens must cover the line exactly")
        }
    }

    func testTokenColoursOfATestCaseLine() {
        let line = "Test Case '-[LoginFailTestsTests.LoginTests testValidCredentials]' passed (0.020 seconds)."
        let tokens = LogClassifier.tokens(for: line, kind: .casePassed)
        XCTAssertEqual(tokens, [
            LogToken("Test Case", .keyword), LogToken(" ", .plain), LogToken("'", .string), LogToken("-[", .plain),
            LogToken("LoginFailTestsTests.LoginTests", .type), LogToken(" ", .plain), LogToken("testValidCredentials", .identifier),
            LogToken("]", .plain), LogToken("'", .string), LogToken(" ", .plain), LogToken("passed", .success), LogToken(" (", .plain),
            LogToken("0.020", .number), LogToken(" seconds).", .plain)
        ])
    }

    func testFailedCaseAndSuiteWords() {
        let failed = LogClassifier.tokens(for: "Test Case '-[A.B c]' failed (0.030 seconds).", kind: .caseFailed)
        XCTAssertTrue(failed.contains(LogToken("failed", .error)))
        let suite = LogClassifier.tokens(for: "Test Suite 'LoginTests' failed at 2026-09-29 16:22:55 +0000.", kind: .suiteFinished(passed: false))
        XCTAssertEqual(suite.first, LogToken("Test Suite", .keyword))
        XCTAssertTrue(suite.contains(LogToken("'LoginTests'", .string)))
        XCTAssertTrue(suite.contains(LogToken("failed", .error)))
    }

    func testExecutedLineHighlightsNumbersAndOnlyRealFailures() {
        let bad = LogClassifier.tokens(for: "\t Executed 8 tests, with 2 failures (0 unexpected) in 0.412 (0.418) seconds", kind: .executed(failed: true))
        XCTAssertEqual(bad.filter { $0.style == .number }.map(\.text), ["8", "2", "0", "0.412", "0.418"])
        XCTAssertTrue(bad.contains(LogToken("failures", .error)))
        let good = LogClassifier.tokens(for: "Executed 8 tests, with 0 failures (0 unexpected) in 0.412 (0.418) seconds", kind: .executed(failed: false))
        XCTAssertFalse(good.contains { $0.style == .error }, "0 failures is not red")
    }

    func testFailureDetailLineTokens() {
        let line = "/Users/dev/Demo/LoginTests.swift:42: error: -[A.B c] : XCTAssertEqual failed"
        let tokens = LogClassifier.tokens(for: line, kind: .failure)
        XCTAssertEqual(tokens.first, LogToken("/Users/dev/Demo/LoginTests.swift:42:", .identifier))
        XCTAssertTrue(tokens.contains(LogToken("error:", .error)))
    }

    func testSummaryLinesAreSingleColours() {
        XCTAssertEqual(LogClassifier.tokens(for: "** TEST SUCCEEDED **", kind: .testSucceeded), [LogToken("** TEST SUCCEEDED **", .success)])
        XCTAssertEqual(LogClassifier.tokens(for: "** TEST FAILED **", kind: .testFailed), [LogToken("** TEST FAILED **", .error)])
        XCTAssertEqual(LogClassifier.tokens(for: "# MobileLab job x", kind: .header), [LogToken("# MobileLab job x", .comment)])
        XCTAssertEqual(LogClassifier.tokens(for: "", kind: .plain), [])
    }

    func testSwiftStyleTestNames() {
        let tokens = LogClassifier.tokens(for: "Test case 'LoginTests.testA()' passed on 'iPhone 15' (0.001 seconds)", kind: .casePassed)
        XCTAssertTrue(tokens.contains(LogToken("LoginTests", .type)))
        XCTAssertTrue(tokens.contains(LogToken("testA()", .identifier)) || tokens.contains(LogToken("testA", .identifier)))
    }

    // MARK: Splitting and documents

    func testSplitLines() {
        XCTAssertEqual(LogDocument.splitLines(""), [])
        XCTAssertEqual(LogDocument.splitLines("a"), ["a"])
        XCTAssertEqual(LogDocument.splitLines("a\nb\n"), ["a", "b"])
        XCTAssertEqual(LogDocument.splitLines("a\r\nb\r\n"), ["a", "b"])
        XCTAssertEqual(LogDocument.splitLines("a\r\n\r\nb"), ["a", "", "b"])
        XCTAssertEqual(LogDocument.splitLines("progress 10%\rprogress 20%\n"), ["progress 10%", "progress 20%"])
        XCTAssertEqual(LogDocument.splitLines("a\n\n"), ["a", ""])
        XCTAssertEqual(LogDocument.splitLines("\n** TEST FAILED **\n"), ["", "** TEST FAILED **"], "an event message may open with a blank line")
        XCTAssertEqual(LogDocument.splitLines("\n"), [""])
        XCTAssertEqual(LogDocument.splitLines("a\n\nb"), ["a", "", "b"])
    }

    func testDocumentNumbersAndClassifiesRealOutput() throws {
        let output = try Fixture.decode(JobOutput.self, "output-fail")
        let document = LogDocument(text: output.text)
        XCTAssertEqual(document.lines.map(\.number), Array(1...document.count))
        XCTAssertTrue(document.lines[0].text.hasPrefix("# MobileLab job"))
        XCTAssertEqual(document.lines[0].info.kind, .header)

        let failures = document.lines.filter { $0.info.kind == .failure }
        XCTAssertEqual(failures.count, 2)
        XCTAssertEqual(failures.map { $0.info.pill ?? "" }, [
            "XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")", "XCTAssertEqual failed: (\"Welcome\") is not equal to (\"Error\")"
        ])
        XCTAssertEqual(document.lines.filter { $0.info.gutter == .failed }.count, 2)
        XCTAssertEqual(document.lines.filter { $0.info.gutter == .passed }.count, 6)
        XCTAssertTrue(document.lines.contains { $0.info.kind == .testFailed })
        XCTAssertEqual(document.failureLineNumbers.count, 3, "two assertions and the closing ** TEST FAILED **")
        XCTAssertEqual(document.lineNumber(containing: "LoginTests.swift:42"), failures[0].number)
        XCTAssertNil(document.lineNumber(containing: "nowhere.swift:1"))
        XCTAssertNil(document.lineNumber(containing: ""))
    }

    func testPassingOutputHasNoErrors() throws {
        let document = LogDocument(text: try Fixture.decode(JobOutput.self, "output-pass").text)
        XCTAssertTrue(document.failureLineNumbers.isEmpty)
        XCTAssertTrue(document.lines.contains { $0.info.kind == .testSucceeded })
        XCTAssertEqual(document.lines.filter { $0.info.gutter == .passed }.count, 8)
    }

    func testBuildErrorOutput() throws {
        let document = LogDocument(text: try Fixture.decode(JobOutput.self, "output-missing").text)
        XCTAssertFalse(document.failureLineNumbers.isEmpty)
        XCTAssertTrue(document.lines.contains { $0.info.kind == .error || $0.info.kind == .buildFailed })
    }

    func testTruncatedTailDropsTheCutLine() {
        let document = LogDocument(text: "artial line\nfull line 1\nfull line 2\n", truncated: true)
        XCTAssertTrue(document.isTruncated)
        XCTAssertEqual(document.lines.map(\.text), ["full line 1", "full line 2"])
        XCTAssertEqual(document.lines.first?.number, 1)
    }

    func testAppendingLiveText() {
        var document = LogDocument(text: "one\ntwo\n")
        document.append(text: "three\nfour\n")
        document.append(text: "\n** TEST FAILED **\n")
        XCTAssertEqual(document.lines.map(\.text), ["one", "two", "three", "four", "", "** TEST FAILED **"])
        XCTAssertEqual(document.lines.last?.number, 6)
    }

    func testFilters() throws {
        let document = LogDocument(text: try Fixture.decode(JobOutput.self, "output-fail").text)
        XCTAssertEqual(document.filtered(.all).count, document.count)
        let errors = document.filtered(.errors)
        XCTAssertTrue(errors.allSatisfy { $0.info.kind == .failure || $0.info.kind == .caseFailed || $0.info.kind == .testFailed || $0.info.kind == .executed(failed: true) || $0.info.kind == .suiteFinished(passed: false) })
        XCTAssertGreaterThanOrEqual(errors.count, 5)
        let tests = document.filtered(.tests)
        XCTAssertTrue(tests.contains { $0.info.kind == .casePassed })
        XCTAssertFalse(tests.contains { $0.info.kind == .header })
        let build = document.filtered(.build)
        XCTAssertTrue(build.contains { $0.info.kind == .header })
        XCTAssertFalse(build.contains { $0.info.kind == .casePassed })
        XCTAssertEqual(document.filtered(.all, query: "testInvalidPassword").count, 3, "started, assertion, failed")
        XCTAssertEqual(document.filtered(.errors, query: "CHECKOUT").count, 3, "suite line, assertion and failed case: case-insensitive, and combined with the popup filter")
    }

    // MARK: Snapshot + live merging

    func testMergingDropsLiveLinesTheSnapshotAlreadyHas() {
        let snapshot = JobOutput(text: "a\nb\nc\nd\n", sizeBytes: 8, attempt: 1)
        let document = LogDocument.merging(snapshot: snapshot, live: ["c", "d", "e", "f"])
        XCTAssertEqual(document.lines.map(\.text), ["a", "b", "c", "d", "e", "f"])
    }

    func testMergingWithNoOverlapAppendsEverything() {
        let document = LogDocument.merging(snapshot: JobOutput(text: "a\nb\n"), live: ["x", "y"])
        XCTAssertEqual(document.lines.map(\.text), ["a", "b", "x", "y"])
    }

    func testMergingWithEmptySnapshotOrEmptyLive() {
        XCTAssertEqual(LogDocument.merging(snapshot: JobOutput(text: ""), live: ["x"]).lines.map(\.text), ["x"])
        XCTAssertEqual(LogDocument.merging(snapshot: JobOutput(text: "a\n"), live: []).lines.map(\.text), ["a"])
        XCTAssertTrue(LogDocument.merging(snapshot: JobOutput(text: ""), live: []).isEmpty)
    }

    func testMergingWhenLiveIsFullyContained() {
        let document = LogDocument.merging(snapshot: JobOutput(text: "a\nb\nc\n"), live: ["b", "c"])
        XCTAssertEqual(document.lines.map(\.text), ["a", "b", "c"])
    }

    func testMergingPrefersTheLongestOverlap() {
        // Repeated lines: the snapshot ends "x x" and live starts "x x y": overlap is 2, not 1.
        let document = LogDocument.merging(snapshot: JobOutput(text: "a\nx\nx\n"), live: ["x", "x", "y"])
        XCTAssertEqual(document.lines.map(\.text), ["a", "x", "x", "y"])
    }

    // MARK: Job log state

    private func outputEvent(_ message: String, job: String = "j1", id: Int = 1) -> EngineEvent {
        EngineEvent(id: id, source: "xcodebuild", type: "log", action: "output", message: message, timestamp: date("2026-09-29T16:00:00Z"), jobId: job)
    }

    func testLiveLinesBeforeTheSnapshotAreBufferedThenMerged() {
        var state = JobLogState()
        state.reset(jobID: "j1")
        XCTAssertTrue(state.isLoading)
        state.appendLive(outputEvent("c\nd\n"))
        state.appendLive(outputEvent("e\n"))
        XCTAssertTrue(state.document.isEmpty, "nothing is shown until the snapshot arrives")
        state.applySnapshot(JobOutput(text: "a\nb\nc\n", sizeBytes: 6, attempt: 1))
        XCTAssertFalse(state.isLoading)
        XCTAssertEqual(state.document.lines.map(\.text), ["a", "b", "c", "d", "e"])
        state.appendLive(outputEvent("f\n"))
        XCTAssertEqual(state.document.lines.last?.text, "f")
    }

    func testEventsOfOtherJobsAndNonOutputEventsAreIgnored() {
        var state = JobLogState()
        state.reset(jobID: "j1")
        state.applySnapshot(JobOutput(text: "a\n"))
        state.appendLive(outputEvent("nope", job: "other"))
        state.appendLive(EngineEvent(id: 2, source: "scheduler", type: "log", action: "job_finished", message: "done", timestamp: date("2026-09-29T16:00:00Z"), jobId: "j1"))
        XCTAssertEqual(state.document.lines.map(\.text), ["a"])
    }

    func testFinalTextReplacesDriftedContent() {
        var state = JobLogState()
        state.reset(jobID: "j1")
        state.applySnapshot(JobOutput(text: "a\nb\n"))
        state.appendLive(outputEvent("b\n"))
        XCTAssertEqual(state.document.count, 3, "a doubled line can slip in while streaming")
        state.replaceWithFinal(JobOutput(text: "a\nb\nc\n"))
        XCTAssertEqual(state.document.lines.map(\.text), ["a", "b", "c"])
    }

    func testFailureAndReset() {
        var state = JobLogState()
        state.reset(jobID: "j1")
        state.failed("Job not found: j1")
        XCTAssertEqual(state.error, "Job not found: j1")
        XCTAssertFalse(state.isLoading)
        state.reset(jobID: nil)
        XCTAssertNil(state.error)
        XCTAssertNil(state.jobID)
        XCTAssertFalse(state.isLoading)
    }

    func testRunningTestNameFollowsStartedAndFinishedLines() {
        var state = JobLogState()
        state.reset(jobID: "j1")
        state.applySnapshot(JobOutput(text: """
        Test Case '-[App.LoginTests testA]' started.
        Test Case '-[App.LoginTests testA]' passed (0.020 seconds).
        Test Case '-[App.LoginTests testB]' started.

        """))
        XCTAssertEqual(state.runningTestName, "testB")
        state.appendLive(outputEvent("Test Case '-[App.LoginTests testB]' failed (0.010 seconds).\n"))
        XCTAssertNil(state.runningTestName)
        state.appendLive(outputEvent("Test Case '-[App.LoginTests testC]' started.\n"))
        XCTAssertEqual(state.runningTestName, "testC")
    }

    func testTestCaseNameParsing() {
        XCTAssertEqual(TestCaseName.parse("Test Case '-[LoginFailTestsTests.LoginTests testValidCredentials]' started."), TestCaseName(className: "LoginFailTestsTests.LoginTests", method: "testValidCredentials"))
        XCTAssertEqual(TestCaseName.parse("Test case 'LoginTests.testA()' passed on 'iPhone 15' (0.001 seconds)"), TestCaseName(className: "LoginTests", method: "testA"))
        XCTAssertNil(TestCaseName.parse("Test Suite 'X' started"))
        XCTAssertNil(TestCaseName.parse("random"))
    }

    func testSourceLocationsAndFailureMessages() {
        XCTAssertEqual(SourceLocation.parseLeading("LoginTests.swift:42 XCTAssertEqual failed"), SourceLocation(file: "LoginTests.swift", line: 42, column: nil))
        XCTAssertEqual(SourceLocation.parseLeading("/Users/x/File.swift:12:5: error: bad"), SourceLocation(file: "File.swift", line: 12, column: 5))
        XCTAssertNil(SourceLocation.parseLeading("no location here"))
        XCTAssertNil(SourceLocation.parseLeading("word:notanumber more"))
        let message = FailureMessage.parse("LoginTests.swift:42 XCTAssertEqual failed: (\"a\") is not equal to (\"b\")\nCheckoutTests.swift:9 second")
        XCTAssertEqual(message.location?.display, "LoginTests.swift:42")
        XCTAssertEqual(message.text, "XCTAssertEqual failed: (\"a\") is not equal to (\"b\")")
        XCTAssertEqual(FailureMessage.parse("plain text failure"), FailureMessage(location: nil, text: "plain text failure"))
    }
}
