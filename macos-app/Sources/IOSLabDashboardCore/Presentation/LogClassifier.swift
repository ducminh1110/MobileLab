import Foundation

/// What a line of xcodebuild output is. Decided with prefix checks only, so classifying a
/// hundred-thousand-line log stays cheap.
public enum LogLineKind: Equatable, Sendable {
    case plain
    /// `# MobileLab job ...`, the first line the backend writes.
    case header
    case suiteStarted
    case suiteFinished(passed: Bool)
    case caseStarted
    case casePassed
    case caseFailed
    case caseSkipped
    /// `File.swift:42: error: -[Class method] : XCTAssertEqual failed ...`
    case failure
    /// A compiler or tool error.
    case error
    case warning
    /// `Executed 8 tests, with 2 failures ...`
    case executed(failed: Bool)
    case testSucceeded
    case testFailed
    case buildFailed
}

public enum TokenStyle: Equatable, Sendable {
    case plain
    case comment
    case keyword
    case string
    case number
    case type
    case identifier
    case error
    case success
    case warning
}

public struct LogToken: Equatable, Sendable {
    public var text: String
    public var style: TokenStyle

    public init(_ text: String, _ style: TokenStyle) {
        self.text = text
        self.style = style
    }
}

/// A diamond in the gutter of a test case line (like the diamonds in Xcode's test source editor).
public enum GutterMark: Equatable, Sendable {
    case passed
    case failed
    case skipped
}

public struct LogLineInfo: Equatable, Sendable {
    public var kind: LogLineKind
    public var gutter: GutterMark?
    /// Text for the red inline pill at the right edge (the assertion message).
    public var pill: String?

    /// Lines that get the `line-error` background.
    public var isErrorLine: Bool {
        switch kind {
        case .failure, .error, .testFailed, .buildFailed: return true
        default: return false
        }
    }
}

/// Which lines the console shows (its popup: All Output, Errors, Build, Tests).
public enum LogFilter: String, CaseIterable, Sendable, Codable {
    case all
    case errors
    case build
    case tests

    public var title: String {
        switch self {
        case .all: return "All Output"
        case .errors: return "Errors"
        case .build: return "Build"
        case .tests: return "Tests"
        }
    }

    public func includes(_ kind: LogLineKind) -> Bool {
        switch self {
        case .all: return true
        case .errors:
            switch kind {
            case .failure, .error, .caseFailed, .testFailed, .buildFailed, .warning: return true
            case .executed(let failed): return failed
            case .suiteFinished(let passed): return !passed
            default: return false
            }
        case .tests:
            switch kind {
            case .suiteStarted, .suiteFinished, .caseStarted, .casePassed, .caseFailed, .caseSkipped, .failure, .executed, .testSucceeded, .testFailed: return true
            default: return false
            }
        case .build:
            switch kind {
            case .suiteStarted, .suiteFinished, .caseStarted, .casePassed, .caseFailed, .caseSkipped, .failure, .executed, .testSucceeded, .testFailed: return false
            default: return true
            }
        }
    }
}

public enum LogClassifier {
    // MARK: Classification

    public static func classify(_ line: String) -> LogLineInfo {
        let trimmed = line.drop(while: { $0 == " " || $0 == "\t" })

        if trimmed.hasPrefix("# MobileLab") { return LogLineInfo(kind: .header) }

        if trimmed.hasPrefix("Test Suite '") {
            if trimmed.contains("' passed at") { return LogLineInfo(kind: .suiteFinished(passed: true)) }
            if trimmed.contains("' failed at") { return LogLineInfo(kind: .suiteFinished(passed: false)) }
            return LogLineInfo(kind: .suiteStarted)
        }

        if trimmed.hasPrefix("Test Case '") || trimmed.hasPrefix("Test case '") {
            if trimmed.contains("' passed") { return LogLineInfo(kind: .casePassed, gutter: .passed) }
            if trimmed.contains("' failed") { return LogLineInfo(kind: .caseFailed, gutter: .failed) }
            if trimmed.contains("' skipped") { return LogLineInfo(kind: .caseSkipped, gutter: .skipped) }
            return LogLineInfo(kind: .caseStarted)
        }

        if trimmed.hasPrefix("** TEST SUCCEEDED **") { return LogLineInfo(kind: .testSucceeded) }
        if trimmed.hasPrefix("** TEST FAILED **") { return LogLineInfo(kind: .testFailed) }
        if trimmed.hasPrefix("** BUILD FAILED **") || trimmed.hasPrefix("** TEST BUILD FAILED **") { return LogLineInfo(kind: .buildFailed) }

        if trimmed.hasPrefix("Executed ") && trimmed.contains(" test") {
            return LogLineInfo(kind: .executed(failed: executedFailureCount(String(trimmed)) > 0))
        }

        if let swiftTesting = classifySwiftTesting(trimmed) { return swiftTesting }

        if let errorRange = trimmed.range(of: ": error: ") ?? trimmed.range(of: "error: ") {
            let message = String(trimmed[errorRange.upperBound...])
            if SourceLocation.parseLeading(String(trimmed)) != nil {
                return LogLineInfo(kind: .failure, pill: pillText(fromErrorMessage: message))
            }
            if trimmed.hasPrefix("error: ") || trimmed.hasPrefix("xcodebuild: error: ") || trimmed.hasPrefix("fatal error") {
                return LogLineInfo(kind: .error, pill: message)
            }
            return LogLineInfo(kind: .error, pill: pillText(fromErrorMessage: message))
        }
        if trimmed.contains(": warning: ") || trimmed.hasPrefix("warning: ") { return LogLineInfo(kind: .warning) }

        return LogLineInfo(kind: .plain)
    }

    /// `-[Class method] : XCTAssertEqual failed: ...` becomes `XCTAssertEqual failed: ...`.
    static func pillText(fromErrorMessage message: String) -> String {
        if message.hasPrefix("-["), let bracket = message.range(of: "] : ") { return String(message[bracket.upperBound...]) }
        if let paren = message.range(of: "() : ") { return String(message[paren.upperBound...]) }
        return message
    }

    static func executedFailureCount(_ line: String) -> Int {
        // "Executed 8 tests, with 2 failures (0 unexpected) in 0.412 (0.418) seconds"
        guard let with = line.range(of: ", with ") else { return 0 }
        let digits = line[with.upperBound...].prefix(while: { $0.isNumber })
        return Int(digits) ?? 0
    }

    /// Swift Testing prints `<symbol> Test "name" passed after 0.001 seconds.` where the symbol is a glyph.
    private static func classifySwiftTesting(_ trimmed: Substring) -> LogLineInfo? {
        guard let first = trimmed.first, !first.isLetter, !first.isNumber, !first.isWhitespace, first != "/", first != "*", first != "#", first != "-" else { return nil }
        let rest = trimmed.dropFirst().drop(while: { $0 == " " })
        guard rest.hasPrefix("Test ") else { return nil }
        if rest.contains(" passed after ") { return LogLineInfo(kind: .casePassed, gutter: .passed) }
        if rest.contains(" failed after ") { return LogLineInfo(kind: .caseFailed, gutter: .failed) }
        if rest.contains(" skipped") { return LogLineInfo(kind: .caseSkipped, gutter: .skipped) }
        if let issue = rest.range(of: " recorded an issue") {
            let message = rest[issue.upperBound...].drop(while: { $0 == " " || $0 == ":" })
            return LogLineInfo(kind: .failure, pill: String(message.split(separator: ":", maxSplits: 1, omittingEmptySubsequences: true).last ?? message))
        }
        return nil
    }

    // MARK: Tokens

    /// Splits a line into styled runs. The runs always concatenate back to exactly `line`.
    public static func tokens(for line: String, kind: LogLineKind) -> [LogToken] {
        if line.isEmpty { return [] }
        switch kind {
        case .header: return [LogToken(line, .comment)]
        case .testSucceeded: return [LogToken(line, .success)]
        case .testFailed, .buildFailed: return [LogToken(line, .error)]
        default: break
        }

        var builder = TokenBuilder()
        let chars = Array(line)
        var i = 0
        let firstContent = chars.firstIndex(where: { $0 != " " && $0 != "\t" }) ?? chars.count
        let numbersMatter: Bool
        switch kind {
        case .casePassed, .caseFailed, .caseSkipped, .executed: numbersMatter = true
        default: numbersMatter = false
        }
        let isDiagnostic = kind == .failure || kind == .error || kind == .warning

        while i < chars.count {
            let c = chars[i]

            if i == firstContent, let keyword = leadingKeyword(chars, at: i) {
                builder.add(String(chars[i..<(i + keyword)]), .keyword)
                i += keyword
                continue
            }

            if isDiagnostic, i == firstContent, let path = leadingLocation(chars, at: i) {
                builder.add(String(chars[i..<(i + path)]), .identifier)
                i += path
                continue
            }

            if c == "'" || c == "\"" {
                if let close = closingQuote(chars, from: i) {
                    builder.append(quotedTokens(Array(chars[i...close])))
                    i = close + 1
                    continue
                }
            }

            if isWordStart(chars, at: i) {
                if let (length, style) = statusWord(chars, at: i, kind: kind) {
                    builder.add(String(chars[i..<(i + length)]), style)
                    i += length
                    continue
                }
            }

            if numbersMatter, c.isNumber, i == 0 || !(chars[i - 1].isLetter || chars[i - 1] == "_" || chars[i - 1] == "-" || chars[i - 1] == ":") {
                var j = i
                while j < chars.count, chars[j].isNumber || chars[j] == "." { j += 1 }
                while j > i, chars[j - 1] == "." { j -= 1 }  // a sentence-ending dot is not part of the number
                if j > i {
                    builder.add(String(chars[i..<j]), .number)
                    i = j
                    continue
                }
            }

            builder.add(String(c), .plain)
            i += 1
        }
        return builder.tokens
    }

    // MARK: Scanner helpers

    private static let keywords: [[Character]] = [
        "Testing started", "Test Suite", "Test Case", "Test case", "Executed"
    ].map { Array($0) }

    private static func leadingKeyword(_ chars: [Character], at index: Int) -> Int? {
        for keyword in keywords where matches(chars, at: index, keyword) { return keyword.count }
        return nil
    }

    private static func matches(_ chars: [Character], at index: Int, _ word: [Character]) -> Bool {
        guard index + word.count <= chars.count else { return false }
        for k in 0..<word.count where chars[index + k] != word[k] { return false }
        return true
    }

    /// `/path/File.swift:42:` at the start of a diagnostic: the path and line, stopping before the space.
    private static func leadingLocation(_ chars: [Character], at index: Int) -> Int? {
        var j = index
        while j < chars.count, chars[j] != " " { j += 1 }
        let token = String(chars[index..<j])
        guard token.contains(":"), token.contains(".") else { return nil }
        return j - index
    }

    private static func closingQuote(_ chars: [Character], from index: Int) -> Int? {
        let quote = chars[index]
        var j = index + 1
        while j < chars.count {
            if chars[j] == quote { return j }
            j += 1
        }
        return nil
    }

    private static func isWordStart(_ chars: [Character], at index: Int) -> Bool {
        guard chars[index].isLetter else { return false }
        return index == 0 || !(chars[index - 1].isLetter || chars[index - 1].isNumber || chars[index - 1] == "_")
    }

    private static func statusWord(_ chars: [Character], at index: Int, kind: LogLineKind) -> (Int, TokenStyle)? {
        let candidates: [(String, TokenStyle)] = [
            ("passed", .success), ("succeeded", .success), ("failed", .error), ("failures", .error), ("failure", .error),
            ("skipped", .warning), ("error:", .error), ("warning:", .warning)
        ]
        for (word, style) in candidates {
            let letters = Array(word)
            guard matches(chars, at: index, letters) else { continue }
            let end = index + letters.count
            if let last = letters.last, last != ":" {
                if end < chars.count, chars[end].isLetter || chars[end].isNumber || chars[end] == "_" { continue }
            }
            // "failures" only matters on the Executed line, where 0 failures is not red.
            if word == "failures" || word == "failure" {
                if case .executed(let failed) = kind, failed { return (letters.count, style) }
                continue
            }
            switch kind {
            case .plain, .header, .caseStarted, .suiteStarted: return nil
            default: return (letters.count, style)
            }
        }
        return nil
    }

    /// Splits a quoted name such as `'-[Module.Class method]'` into string, type and identifier runs.
    private static func quotedTokens(_ quoted: [Character]) -> [LogToken] {
        let text = String(quoted)
        let quote = String(quoted[0])
        let inner = String(quoted.dropFirst().dropLast())

        // -[Module.Class method]
        if inner.hasPrefix("-["), inner.hasSuffix("]"), let space = inner.firstIndex(of: " ") {
            let classPart = inner[inner.index(inner.startIndex, offsetBy: 2)..<space]
            let methodPart = inner[inner.index(after: space)..<inner.index(before: inner.endIndex)]
            return [
                LogToken(quote, .string), LogToken("-[", .plain), LogToken(String(classPart), .type), LogToken(" ", .plain),
                LogToken(String(methodPart), .identifier), LogToken("]", .plain), LogToken(quote, .string)
            ]
        }
        // Class.method()  or  Module.Class.method()
        if inner.hasSuffix("()"), let dot = inner.lastIndex(of: ".") {
            let classPart = inner[..<dot]
            let methodPart = inner[inner.index(after: dot)...]
            return [
                LogToken(quote, .string), LogToken(String(classPart), .type), LogToken(".", .plain),
                LogToken(String(methodPart), .identifier), LogToken(quote, .string)
            ]
        }
        return [LogToken(text, .string)]
    }

    private struct TokenBuilder {
        var tokens: [LogToken] = []

        mutating func add(_ text: String, _ style: TokenStyle) {
            if text.isEmpty { return }
            if let last = tokens.last, last.style == style {
                tokens[tokens.count - 1].text += text
            } else {
                tokens.append(LogToken(text, style))
            }
        }

        mutating func append(_ more: [LogToken]) {
            for token in more { add(token.text, token.style) }
        }
    }
}

// MARK: - Source locations

/// `File.swift:42`, as found in assertion messages and compiler errors.
public struct SourceLocation: Equatable, Sendable {
    public var file: String
    public var line: Int
    public var column: Int?

    public var display: String { "\(file):\(line)" }

    /// The location at the very start of a message: `LoginTests.swift:42 XCTAssertEqual failed` or
    /// `/Users/x/File.swift:12:5: error: ...`. The file is reduced to its last path component.
    public static func parseLeading(_ text: String) -> SourceLocation? {
        let trimmed = text.drop(while: { $0 == " " || $0 == "\t" })
        let head = trimmed.prefix(while: { $0 != " " })
        // "path:line", "path:line:", "path:line:col:"
        let parts = head.split(separator: ":", omittingEmptySubsequences: false).map(String.init)
        guard parts.count >= 2, let line = Int(parts[1]), !parts[0].isEmpty, parts[0].contains(".") else { return nil }
        let column = parts.count >= 3 ? Int(parts[2]) : nil
        let file = parts[0].split(separator: "/").last.map(String.init) ?? parts[0]
        return SourceLocation(file: file, line: line, column: column)
    }
}

/// A test failure message split into where and what.
public struct FailureMessage: Equatable, Sendable {
    public var location: SourceLocation?
    public var text: String

    /// `LoginTests.swift:42 XCTAssertEqual failed: ...` (possibly several such lines) becomes location + text.
    public static func parse(_ message: String) -> FailureMessage {
        let first = message.split(separator: "\n", maxSplits: 1, omittingEmptySubsequences: true).first.map(String.init) ?? message
        guard let location = SourceLocation.parseLeading(first) else {
            return FailureMessage(location: nil, text: message.trimmingCharacters(in: .whitespacesAndNewlines))
        }
        var rest = first.drop(while: { $0 == " " })
        rest = rest.drop(while: { $0 != " " })  // skip the "file:line" token
        var text = rest.drop(while: { $0 == " " })
        // Compiler errors read "File.swift:12: message"
        if text.hasPrefix("error: ") { text = text.dropFirst(7) }
        return FailureMessage(location: location, text: String(text))
    }
}

// MARK: - Document

public struct LogLine: Equatable, Sendable, Identifiable {
    /// 1-based, as shown in the gutter.
    public var number: Int
    public var text: String
    public var info: LogLineInfo

    public var id: Int { number }
}

/// A job's output as an editor would hold it: numbered, classified lines that grow while the job runs.
public struct LogDocument: Equatable, Sendable {
    public private(set) var lines: [LogLine] = []
    /// The backend returned only the tail of a large log; earlier output is not shown.
    public private(set) var isTruncated = false

    public init() {}

    public init(text: String, truncated: Bool = false) {
        isTruncated = truncated
        var pieces = LogDocument.splitLines(text)
        // A tail cut at a byte offset starts in the middle of a line: drop that fragment.
        if truncated, !pieces.isEmpty { pieces.removeFirst() }
        appendLines(pieces)
    }

    public var isEmpty: Bool { lines.isEmpty }
    public var count: Int { lines.count }

    /// `"a\nb\n"` gives `["a", "b"]`; `"\nx\n"` gives `["", "x"]`. Handles `\r\n` (which Swift treats as one
    /// Character, so it has to be named as a separator) and a bare `\r`.
    public static func splitLines(_ text: String) -> [String] {
        if text.isEmpty { return [] }
        var pieces = text.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" || $0 == "\r\n" || $0 == "\r" }).map(String.init)
        // Text that ends with a separator leaves one empty piece after it, which is not a line.
        if pieces.last == "" { pieces.removeLast() }
        return pieces
    }

    public mutating func append(text: String) {
        appendLines(LogDocument.splitLines(text))
    }

    public mutating func appendLines(_ pieces: [String]) {
        lines.reserveCapacity(lines.count + pieces.count)
        for piece in pieces {
            lines.append(LogLine(number: lines.count + 1, text: piece, info: LogClassifier.classify(piece)))
        }
    }

    /// Builds the document from a fetched snapshot and the live lines that arrived while it was being fetched.
    /// Live lines the snapshot already contains (they were written before it was read) are dropped by matching
    /// the end of the snapshot against the start of the live lines.
    public static func merging(snapshot: JobOutput, live: [String]) -> LogDocument {
        var document = LogDocument(text: snapshot.text, truncated: snapshot.truncated)
        let tail = document.lines.map(\.text)
        var overlap = 0
        let maxOverlap = min(tail.count, live.count)
        if maxOverlap > 0 {
            for k in stride(from: maxOverlap, through: 1, by: -1) where Array(tail.suffix(k)) == Array(live.prefix(k)) {
                overlap = k
                break
            }
        }
        document.appendLines(Array(live.dropFirst(overlap)))
        return document
    }

    /// First line containing `needle` (case-sensitive), for "jump to this failure".
    public func lineNumber(containing needle: String) -> Int? {
        guard !needle.isEmpty else { return nil }
        return lines.first(where: { $0.text.contains(needle) })?.number
    }

    /// Lines the console shows for a filter and a text query.
    public func filtered(_ filter: LogFilter, query: String = "") -> [LogLine] {
        let needle = query.trimmingCharacters(in: .whitespaces)
        if filter == .all && needle.isEmpty { return lines }
        return lines.filter { line in
            filter.includes(line.info.kind) && (needle.isEmpty || line.text.range(of: needle, options: .caseInsensitive) != nil)
        }
    }

    public var failureLineNumbers: [Int] { lines.filter { $0.info.isErrorLine }.map(\.number) }
}
