#include "LogClassifier.h"
#include <QRegularExpression>
#include <algorithm>

namespace LogSyntax {

namespace {
void add(QVector<Span> &v, int start, int len, Token t) {
    if (len > 0) v.push_back({start, len, t});
}

// Resolve overlaps: earlier spans win, result sorted.
QVector<Span> normalise(QVector<Span> in, int length) {
    std::stable_sort(in.begin(), in.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    QVector<Span> out;
    int cursor = 0;
    for (auto s : in) {
        if (s.start < cursor) {
            const int cut = cursor - s.start;
            if (cut >= s.length) continue;
            s.start += cut;
            s.length -= cut;
        }
        if (s.start + s.length > length) s.length = length - s.start;
        if (s.length <= 0) continue;
        out.push_back(s);
        cursor = s.start + s.length;
    }
    return out;
}

void addNumbers(QVector<Span> &spans, const QString &text) {
    static const QRegularExpression re("\\b\\d+(?:\\.\\d+)?\\b");
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        add(spans, m.capturedStart(), m.capturedLength(), Token::Number);
    }
}

void addPaths(QVector<Span> &spans, const QString &text) {
    static const QRegularExpression re("(?:/[\\w.\\-+@]+){2,}(?::\\d+)?");
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        add(spans, m.capturedStart(), m.capturedLength(), Token::Identifier);
    }
}
}

QVector<Span> withGaps(const QVector<Span> &spans, int length) {
    QVector<Span> out;
    int cursor = 0;
    for (const auto &s : spans) {
        if (s.start > cursor) out.push_back({cursor, s.start - cursor, Token::Plain});
        out.push_back(s);
        cursor = s.start + s.length;
    }
    if (cursor < length) out.push_back({cursor, length - cursor, Token::Plain});
    return out;
}

Line classify(const QString &text) {
    static const QRegularExpression caseRe("^(\\s*)(Test Case|Test Suite) '([^']*)' (started|passed|failed|skipped|cancelled)(.*)$");
    static const QRegularExpression errRe("^(.*?)(?::(\\d+))?: error: (.*)$");
    static const QRegularExpression warnRe("^(.*?)(?::(\\d+))?: warning: (.*)$");
    static const QRegularExpression execRe("^(\\s*)(Executed) (\\d+) tests?, with (\\d+) failures?");
    static const QRegularExpression logcatRe("^(\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3})\\s+(\\d+)\\s+(\\d+)\\s+([VDIWEF])\\s+([^:]+?)\\s*:\\s(.*)$");
    static const QRegularExpression nameRe("^-\\[(\\S+) (\\S+)\\]$");
    static const QRegularExpression timeRe("\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d(?:\\.\\d+)?");
    static const QRegularExpression secondsRe("\\((\\d+(?:\\.\\d+)?) seconds\\)");

    Line line;
    QVector<Span> spans;
    const QString t = text;

    if (auto m = caseRe.match(t); m.hasMatch()) {
        const bool isCase = m.captured(2) == "Test Case";
        const QString verb = m.captured(4);
        add(spans, m.capturedStart(2), m.capturedLength(2), Token::Keyword);
        const int q0 = m.capturedStart(3) - 1;
        add(spans, q0, 1, Token::String);
        add(spans, m.capturedEnd(3), 1, Token::String);
        const QString name = m.captured(3);
        if (auto nm = nameRe.match(name); nm.hasMatch()) {
            const int base = m.capturedStart(3);
            add(spans, base, 2, Token::String);
            add(spans, base + nm.capturedStart(1), nm.capturedLength(1), Token::Type);
            add(spans, base + nm.capturedStart(2), nm.capturedLength(2), Token::Identifier);
            add(spans, base + name.size() - 1, 1, Token::String);
        } else {
            add(spans, m.capturedStart(3), m.capturedLength(3), Token::Type);
        }
        const Token vt = verb == "passed" ? Token::Success : (verb == "failed" ? Token::Error : Token::Plain);
        add(spans, m.capturedStart(4), m.capturedLength(4), vt);
        const int restStart = m.capturedStart(5);
        auto it = timeRe.globalMatch(t, restStart);
        while (it.hasNext()) {
            const auto tm = it.next();
            add(spans, tm.capturedStart(), tm.capturedLength(), Token::Number);
        }
        auto sit = secondsRe.globalMatch(t, restStart);
        while (sit.hasNext()) {
            const auto sm = sit.next();
            add(spans, sm.capturedStart(1), sm.capturedLength(1), Token::Number);
        }
        line.caseName = name;
        if (isCase) line.kind = verb == "started" ? Kind::CaseStart : verb == "passed" ? Kind::CasePass : verb == "failed" ? Kind::CaseFail : Kind::CaseSkip;
        else line.kind = verb == "started" ? Kind::SuiteStart : Kind::SuiteEnd;
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (auto m = execRe.match(t); m.hasMatch()) {
        add(spans, m.capturedStart(2), m.capturedLength(2), Token::Keyword);
        add(spans, m.capturedStart(3), m.capturedLength(3), Token::Number);
        add(spans, m.capturedStart(4), m.capturedLength(4), m.captured(4) == "0" ? Token::Number : Token::Error);
        static const QRegularExpression tail("in (\\d+(?:\\.\\d+)?) \\((\\d+(?:\\.\\d+)?)\\) seconds");
        if (auto tm = tail.match(t); tm.hasMatch()) {
            add(spans, tm.capturedStart(1), tm.capturedLength(1), Token::Number);
            add(spans, tm.capturedStart(2), tm.capturedLength(2), Token::Number);
        }
        line.kind = Kind::Executed;
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (t.contains("** TEST SUCCEEDED **")) {
        add(spans, t.indexOf("**"), t.size() - t.indexOf("**"), Token::Success);
        line.kind = Kind::Success;
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (t.contains("** TEST FAILED **") || t.contains("** TEST CANCELLED **")) {
        add(spans, t.indexOf("**"), t.size() - t.indexOf("**"), Token::Error);
        line.kind = Kind::Failure;
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (auto m = errRe.match(t); m.hasMatch()) {
        const QString path = m.captured(1);
        if (!path.isEmpty()) add(spans, 0, m.capturedEnd(2) >= 0 && m.capturedLength(2) > 0 ? m.capturedEnd(2) : m.capturedEnd(1), Token::Identifier);
        const int e = t.indexOf("error:", m.capturedEnd(1));
        add(spans, e, t.size() - e, Token::Error);
        line.kind = Kind::Error;
        line.path = path;
        line.message = m.captured(3);
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (auto m = warnRe.match(t); m.hasMatch()) {
        const int e = t.indexOf("warning:", m.capturedEnd(1));
        add(spans, 0, m.capturedEnd(1), Token::Identifier);
        add(spans, e, t.size() - e, Token::Warning);
        line.kind = Kind::Warning;
        line.path = m.captured(1);
        line.message = m.captured(3);
        line.spans = normalise(spans, t.size());
        return line;
    }
    if (auto m = logcatRe.match(t); m.hasMatch()) {
        const QChar lv = m.captured(4).at(0);
        add(spans, m.capturedStart(1), m.capturedEnd(3) - m.capturedStart(1), Token::Comment);
        add(spans, m.capturedStart(4), 1, lv == 'E' || lv == 'F' ? Token::Error : lv == 'W' ? Token::Warning : Token::Comment);
        add(spans, m.capturedStart(5), m.capturedLength(5), Token::Type);
        if (lv == 'E' || lv == 'F') add(spans, m.capturedStart(6), m.capturedLength(6), Token::Error);
        line.kind = Kind::Logcat;
        line.logcatLevel = lv;
        line.message = m.captured(6);
        line.spans = normalise(spans, t.size());
        return line;
    }
    const QString trimmed = t.trimmed();
    if (trimmed.startsWith('#') || trimmed.startsWith("//")) {
        add(spans, 0, t.size(), Token::Comment);
        line.spans = normalise(spans, t.size());
        return line;
    }
    addPaths(spans, t);
    line.spans = normalise(spans, t.size());
    return line;
}

Line classifyConsole(const QString &text) {
    static const QRegularExpression tagRe("^(\\[[^\\]]+\\])");
    static const QRegularExpression errWords("\\b(error|failed|failure|cannot|refused|unavailable|not found|could not|denied)\\b", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression okWords("\\b(PASS|passed|completed|created|started|stopped)\\b", QRegularExpression::CaseInsensitiveOption);
    Line line;
    QVector<Span> spans;
    if (auto m = tagRe.match(text); m.hasMatch()) add(spans, 0, m.capturedLength(1), Token::Type);
    const int from = spans.isEmpty() ? 0 : spans[0].length;
    const QString rest = text.mid(from);
    if (errWords.match(rest).hasMatch() || rest.contains("FAIL")) {
        add(spans, from, rest.size(), Token::Error);
        line.kind = Kind::Error;
        line.message = rest.trimmed();
    } else if (okWords.match(rest).hasMatch() && !rest.contains("warning", Qt::CaseInsensitive)) {
        add(spans, from, rest.size(), Token::Success);
        line.kind = Kind::Success;
    } else if (rest.contains("warning", Qt::CaseInsensitive) || rest.contains("degraded", Qt::CaseInsensitive)) {
        add(spans, from, rest.size(), Token::Warning);
        line.kind = Kind::Warning;
    }
    line.spans = normalise(spans, text.size());
    return line;
}

QColor colorFor(Token t, const Tokens &k) {
    switch (t) {
    case Token::Comment: return k.synComment;
    case Token::Keyword: return k.synKeyword;
    case Token::String: return k.synString;
    case Token::Number: return k.synNumber;
    case Token::Type: return k.synType;
    case Token::Identifier: return k.synIdentifier;
    case Token::Error: return k.synError;
    case Token::Success: return k.pass;
    case Token::Warning: return k.warn;
    case Token::Plain: break;
    }
    return k.text;
}

}  // namespace LogSyntax
