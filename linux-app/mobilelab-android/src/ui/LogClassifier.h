#pragma once
// Classifies one line of xcodebuild style run output (and logcat / console lines) into a kind and
// syntax spans. Pure functions, no widgets.
#include <QColor>
#include <QString>
#include <QVector>
#include "Theme.h"

namespace LogSyntax {

enum class Token { Plain, Comment, Keyword, String, Number, Type, Identifier, Error, Success, Warning };
enum class Kind { Plain, SuiteStart, SuiteEnd, CaseStart, CasePass, CaseFail, CaseSkip, Executed, Error, Warning, Success, Failure, Logcat };

struct Span {
    int start = 0;
    int length = 0;
    Token token = Token::Plain;
};

struct Line {
    Kind kind = Kind::Plain;
    QVector<Span> spans;   // non overlapping, sorted, gaps are Plain
    QString message;       // Error: text after "error:"
    QString path;          // Error: file that was reported
    QString caseName;      // Case*: "-[avd step]"
    QChar logcatLevel;     // Logcat: V D I W E F
};

Line classify(const QString &text);
// Console lines look like "[runtime] Started ..."; colours errors red and passes green.
Line classifyConsole(const QString &text);
QColor colorFor(Token t, const Tokens &tk);
// Fill the gaps between spans with Plain spans (convenient for painters).
QVector<Span> withGaps(const QVector<Span> &spans, int length);

}  // namespace LogSyntax
