#include <QtTest>
#include "LogClassifier.h"

using namespace LogSyntax;

static Token tokenAt(const Line &l, int pos) {
    for (const auto &s : l.spans)
        if (pos >= s.start && pos < s.start + s.length) return s.token;
    return Token::Plain;
}

class TstLogClassifier : public QObject {
    Q_OBJECT
private slots:
    void suiteLines() {
        const QString t = "Test Suite 'Matrix matrix-1' started at 2026-09-30 10:15:00.123";
        const Line l = classify(t);
        QCOMPARE(l.kind, Kind::SuiteStart);
        QCOMPARE(tokenAt(l, 0), Token::Keyword);                       // "Test Suite"
        QCOMPARE(tokenAt(l, t.indexOf("'Matrix")), Token::String);
        QCOMPARE(tokenAt(l, t.indexOf("2026")), Token::Number);         // timestamp
        const Line e = classify("Test Suite 'pixel' failed at 2026-09-30 10:15:00.123.");
        QCOMPARE(e.kind, Kind::SuiteEnd);
        QCOMPARE(classify("Test Suite 'pixel' passed at 2026-09-30 10:15:00.123.").kind, Kind::SuiteEnd);
    }
    void caseLines() {
        const QString started = "Test Case '-[Pixel_8 boot]' started.";
        Line l = classify(started);
        QCOMPARE(l.kind, Kind::CaseStart);
        QCOMPARE(l.caseName, QString("-[Pixel_8 boot]"));
        QCOMPARE(tokenAt(l, started.indexOf("Pixel_8")), Token::Type);
        QCOMPARE(tokenAt(l, started.indexOf("boot")), Token::Identifier);
        const QString pass = "Test Case '-[Pixel_8 boot]' passed (12.345 seconds).";
        l = classify(pass);
        QCOMPARE(l.kind, Kind::CasePass);
        QCOMPARE(tokenAt(l, pass.indexOf("passed")), Token::Success);
        QCOMPARE(tokenAt(l, pass.indexOf("12.345")), Token::Number);
        const QString fail = "Test Case '-[Pixel_8 abi]' failed (0.310 seconds).";
        l = classify(fail);
        QCOMPARE(l.kind, Kind::CaseFail);
        QCOMPARE(tokenAt(l, fail.indexOf("failed")), Token::Error);
        QCOMPARE(classify("Test Case '-[Pixel_8 api]' skipped (API level unknown).").kind, Kind::CaseSkip);
    }
    void errorLines() {
        const QString t = "/home/u/.android/avd/Pixel_8.avd/config.ini: error: -[Pixel_8 abi] : expected ABI 'arm64-v8a' but the emulator reports 'x86_64'";
        const Line l = classify(t);
        QCOMPARE(l.kind, Kind::Error);
        QVERIFY(l.message.startsWith("-[Pixel_8 abi]"));
        QVERIFY(l.path.endsWith("config.ini"));
        QCOMPARE(tokenAt(l, 3), Token::Identifier);
        QCOMPARE(tokenAt(l, t.indexOf("error:")), Token::Error);
        const Line withLine = classify("src/a.cpp:42: error: boom");
        QCOMPARE(withLine.kind, Kind::Error);
        QCOMPARE(withLine.message, QString("boom"));
        QCOMPARE(withLine.path, QString("src/a.cpp"));
        QCOMPARE(classify("src/a.cpp:42: warning: careful").kind, Kind::Warning);
    }
    void summaryLines() {
        Line l = classify("\t Executed 6 tests, with 1 failures (0 unexpected) in 13.200 (13.200) seconds");
        QCOMPARE(l.kind, Kind::Executed);
        QCOMPARE(tokenAt(l, 2), Token::Keyword);
        QCOMPARE(classify("** TEST SUCCEEDED **").kind, Kind::Success);
        QCOMPARE(classify("** TEST FAILED **").kind, Kind::Failure);
        QCOMPARE(tokenAt(classify("** TEST SUCCEEDED **"), 3), Token::Success);
        QCOMPARE(tokenAt(classify("** TEST FAILED **"), 3), Token::Error);
    }
    void logcatAndComments() {
        Line l = classify("09-30 10:15:00.123  1234  1250 E AndroidRuntime: FATAL EXCEPTION: main");
        QCOMPARE(l.kind, Kind::Logcat);
        QCOMPARE(l.logcatLevel, QChar('E'));
        QCOMPARE(classify("09-30 10:15:00.123  1234  1250 I ActivityManager: start").logcatLevel, QChar('I'));
        l = classify("# a comment");
        QCOMPARE(tokenAt(l, 2), Token::Comment);
        QCOMPARE(classify("plain text").kind, Kind::Plain);
        l = classify("Artifacts in /tmp/mobilelab/run-1/Pixel_8");
        QCOMPARE(tokenAt(l, 16), Token::Identifier);
    }
    void spansAreSortedAndDisjoint() {
        const QStringList lines = {
            "Test Case '-[Pixel_8 boot]' passed (12.345 seconds).",
            "/a/b/c.ini: error: -[x y] : z",
            "Test Suite 'x' started at 2026-09-30 10:15:00.123",
            "\t Executed 6 tests, with 0 failures (0 unexpected) in 13.200 (13.200) seconds",
            "09-30 10:15:00.123  1234  1250 W Tag: text /x/y/z",
        };
        for (const auto &t : lines) {
            const Line l = classify(t);
            int cursor = 0;
            for (const auto &s : l.spans) {
                QVERIFY2(s.start >= cursor, qPrintable(t));
                QVERIFY(s.length > 0 && s.start + s.length <= t.size());
                cursor = s.start + s.length;
            }
            int covered = 0;
            for (const auto &s : withGaps(l.spans, t.size())) covered += s.length;
            QCOMPARE(covered, int(t.size()));
        }
    }
    void consoleLines() {
        Line l = classifyConsole("[runtime] Started Android target Pixel_8 [x86_64, preferred]");
        QCOMPARE(tokenAt(l, 1), Token::Type);
        QCOMPARE(l.kind, Kind::Success);
        l = classifyConsole("[matrix] Pixel_8 FAIL");
        QCOMPARE(l.kind, Kind::Error);
        l = classifyConsole("[runtime] KVM unavailable; accelerated emulator workloads are downgraded");
        QCOMPARE(l.kind, Kind::Error);
        QCOMPARE(classifyConsole("[api] GET /status").kind, Kind::Plain);
    }
    void colours() {
        const Tokens light = Theme::tokens(false), dark = Theme::tokens(true);
        QCOMPARE(colorFor(Token::Keyword, light).name(), QString("#ad3da4"));
        QCOMPARE(colorFor(Token::Keyword, dark).name(), QString("#fc5fa3"));
        QCOMPARE(colorFor(Token::Success, light), light.pass);
        QCOMPARE(colorFor(Token::Plain, dark), dark.text);
    }
};

QTEST_APPLESS_MAIN(TstLogClassifier)
#include "tst_logclassifier.moc"
