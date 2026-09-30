#pragma once
// Data model of one matrix run: run -> target (suite) -> step (test case).
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

enum class RunState { Pending, Running, Passed, Failed, Cancelled, Skipped };

QString runStateName(RunState s);
RunState runStateFromName(const QString &s);

struct StepResult {
    QString name;
    RunState state = RunState::Pending;
    qint64 durationMs = 0;
    QString message;
    int logLine = -1;      // zero based line index in run.log where the step starts
    int failLine = -1;     // line of the error output, if any
};

struct TargetResult {
    QString avd, api, abi, serial, jobId;
    RunState state = RunState::Pending;
    QVector<StepResult> steps;
    QDateTime started, finished;
    qint64 durationMs = 0;
    int logLine = -1;      // first line of this target in the run log
    int logLines = 0;      // number of lines in <artifactDir>/test.log
    QString artifactDir;
    bool startedByUs = false;
    int failedSteps() const;
    int executedSteps() const;
};

struct MatrixRunRecord {
    QString id, dir, logPath, error;
    QDateTime started, finished;
    RunState state = RunState::Pending;
    QVector<TargetResult> targets;
    int logLines = 0;
    int totalSteps() const;
    int failedSteps() const;
    qint64 durationMs() const;
    QJsonObject toJson() const;
    static MatrixRunRecord fromJson(const QJsonObject &o, const QString &dir);
};
