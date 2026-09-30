#include "MatrixRun.h"
#include <QJsonArray>

QString runStateName(RunState s) {
    switch (s) {
    case RunState::Pending: return "pending";
    case RunState::Running: return "running";
    case RunState::Passed: return "passed";
    case RunState::Failed: return "failed";
    case RunState::Cancelled: return "cancelled";
    case RunState::Skipped: return "skipped";
    }
    return "pending";
}

RunState runStateFromName(const QString &s) {
    if (s == "running") return RunState::Running;
    if (s == "passed") return RunState::Passed;
    if (s == "failed") return RunState::Failed;
    if (s == "cancelled") return RunState::Cancelled;
    if (s == "skipped") return RunState::Skipped;
    return RunState::Pending;
}

int TargetResult::failedSteps() const {
    int n = 0;
    for (const auto &s : steps)
        if (s.state == RunState::Failed) ++n;
    return n;
}

int TargetResult::executedSteps() const {
    int n = 0;
    for (const auto &s : steps)
        if (s.state == RunState::Passed || s.state == RunState::Failed) ++n;
    return n;
}

int MatrixRunRecord::totalSteps() const {
    int n = 0;
    for (const auto &t : targets) n += t.executedSteps();
    return n;
}

int MatrixRunRecord::failedSteps() const {
    int n = 0;
    for (const auto &t : targets) n += t.failedSteps();
    return n;
}

qint64 MatrixRunRecord::durationMs() const {
    if (!started.isValid()) return 0;
    const QDateTime end = finished.isValid() ? finished : QDateTime::currentDateTimeUtc();
    return started.msecsTo(end);
}

static QString iso(const QDateTime &d) { return d.isValid() ? d.toUTC().toString(Qt::ISODateWithMs) : QString(); }
static QDateTime fromIso(const QString &s) { return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODateWithMs); }

QJsonObject MatrixRunRecord::toJson() const {
    QJsonArray ts;
    for (const auto &t : targets) {
        QJsonArray steps;
        for (const auto &s : t.steps)
            steps.append(QJsonObject{{"name", s.name}, {"state", runStateName(s.state)}, {"durationMs", double(s.durationMs)},
                                     {"message", s.message}, {"logLine", s.logLine}, {"failLine", s.failLine}});
        ts.append(QJsonObject{{"avd", t.avd}, {"api", t.api}, {"abi", t.abi}, {"job", t.jobId},
                              {"passed", t.state == RunState::Passed}, {"state", runStateName(t.state)},
                              {"serial", t.serial}, {"started", iso(t.started)}, {"finished", iso(t.finished)},
                              {"durationMs", double(t.durationMs)}, {"logLine", t.logLine}, {"logLines", t.logLines},
                              {"artifactDir", t.artifactDir}, {"startedByUs", t.startedByUs}, {"steps", steps}});
    }
    return {{"id", id}, {"artifactDir", dir}, {"state", runStateName(state)}, {"started", iso(started)},
            {"finished", iso(finished)}, {"logLines", logLines}, {"error", error}, {"targets", ts}};
}

MatrixRunRecord MatrixRunRecord::fromJson(const QJsonObject &o, const QString &dir) {
    MatrixRunRecord r;
    r.id = o.value("id").toString();
    r.dir = dir;
    r.logPath = dir + "/run.log";
    r.error = o.value("error").toString();
    r.started = fromIso(o.value("started").toString());
    r.finished = fromIso(o.value("finished").toString());
    r.logLines = o.value("logLines").toInt();
    for (const auto &tv : o.value("targets").toArray()) {
        const auto to = tv.toObject();
        TargetResult t;
        t.avd = to.value("avd").toString();
        t.api = to.value("api").toString();
        t.abi = to.value("abi").toString();
        t.jobId = to.value("job").toString();
        t.serial = to.value("serial").toString();
        t.started = fromIso(to.value("started").toString());
        t.finished = fromIso(to.value("finished").toString());
        t.durationMs = qint64(to.value("durationMs").toDouble());
        t.logLine = to.value("logLine").toInt(-1);
        t.logLines = to.value("logLines").toInt();
        t.artifactDir = to.value("artifactDir").toString();
        t.startedByUs = to.value("startedByUs").toBool();
        // Older run.json files only carry `passed`.
        t.state = to.contains("state") ? runStateFromName(to.value("state").toString())
                                       : (to.value("passed").toBool() ? RunState::Passed : RunState::Failed);
        for (const auto &sv : to.value("steps").toArray()) {
            const auto so = sv.toObject();
            StepResult s;
            s.name = so.value("name").toString();
            s.state = runStateFromName(so.value("state").toString());
            s.durationMs = qint64(so.value("durationMs").toDouble());
            s.message = so.value("message").toString();
            s.logLine = so.value("logLine").toInt(-1);
            s.failLine = so.value("failLine").toInt(-1);
            t.steps.push_back(s);
        }
        r.targets.push_back(t);
    }
    if (o.contains("state")) r.state = runStateFromName(o.value("state").toString());
    else {
        r.state = RunState::Passed;
        for (const auto &t : r.targets)
            if (t.state != RunState::Passed) r.state = RunState::Failed;
    }
    // A run that was interrupted (app quit) must not show as running forever.
    if (r.state == RunState::Running || r.state == RunState::Pending) {
        r.state = RunState::Cancelled;
        if (r.error.isEmpty()) r.error = "interrupted before completion";
        for (auto &t : r.targets)
            if (t.state == RunState::Running || t.state == RunState::Pending) t.state = RunState::Cancelled;
    }
    return r;
}
