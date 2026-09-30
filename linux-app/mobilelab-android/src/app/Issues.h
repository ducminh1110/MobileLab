#pragma once
// Issue navigator data: failed targets of recent runs plus honest probe problems, each with a remedy.
#include <QString>
#include <QVector>
#include "AppContext.h"
#include "Location.h"

struct Issue {
    enum Severity { Error, Warning, Info };
    Severity severity = Info;
    QString id;         // stable, used to keep selection
    QString category;   // "Failed Targets", "Environment"
    QString group;      // e.g. run id or "Android SDK"
    QString title;
    QString detail;
    QString remedy;
    Location where;
};

QVector<Issue> collectIssues(const AppContext &ctx, int maxRuns = 5);
QString remedyForFailure(const QString &step, const QString &message);
