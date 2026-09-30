#include "MatrixExecutor.h"
#include "AndroidEmulator.h"
#include "ArtifactCollector.h"
#include "AsyncProcess.h"
#include "ResourceScheduler.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <csignal>
#include <utility>

namespace {
const QStringList kSteps = {"boot", "abi", "api", "logcat", "screenshot", "shutdown"};

QString stamp() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"); }
QString secs(qint64 ms) { return QString::number(ms / 1000.0, 'f', 3); }

QString safeName(const QString &s) {
    QString r = s;
    r.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return r;
}
}

// ---------------------------------------------------------------------------------------------
// One target of a run: executes the steps sequentially without ever blocking the UI thread.
// ---------------------------------------------------------------------------------------------
class TargetJob final : public QObject {
    Q_OBJECT
public:
    TargetJob(MatrixExecutor *ex, int run, int t) : QObject(ex), m_ex(ex), m_run(run), m_t(t) {}
    QString jobId() { return tr().jobId; }
    bool done() const { return m_done; }

    void start() {
        if (m_started || m_done) return;
        m_started = true;
        m_suite.start();
        tr().state = RunState::Running;
        tr().started = QDateTime::currentDateTimeUtc();
        QDir().mkpath(tr().artifactDir);
        tr().logLine = rec().logLines;
        log(QString("Test Suite '%1' started at %2").arg(tr().avd, stamp()));
        emit m_ex->logMessage("matrix: starting " + tr().avd);
        emit m_ex->runChanged(rec().id);
        m_step = 0;
        runStep();
    }

    void cancel() {
        if (m_done || m_cancelled) return;
        m_cancelled = true;
        if (m_proc) m_proc->abort();
        if (!m_started) {
            for (auto &s : tr().steps) s.state = RunState::Skipped;
            tr().state = RunState::Cancelled;
            m_done = true;
            m_ex->targetDone(m_run, m_t, false);
            return;
        }
        if (m_step < tr().steps.size() && tr().steps[m_step].state == RunState::Running) {
            cur().state = RunState::Cancelled;
            cur().durationMs = m_stepTimer.elapsed();
            log(QString("Test Case '-[%1 %2]' cancelled (%3 seconds).").arg(tr().avd, cur().name, secs(cur().durationMs)));
        }
        for (int i = m_step + 1; i < tr().steps.size(); ++i)
            if (tr().steps[i].state == RunState::Pending) tr().steps[i].state = RunState::Skipped;
        // Do not leave an emulator we launched behind.
        if (tr().startedByUs) {
            if (!tr().serial.isEmpty())
                AsyncProcess::run(m_ex->m_emulator, m_ex->m_emulator->adbPath(), {"-s", tr().serial, "emu", "kill"}, 15000, {});
            else if (m_pid > 0)
                ::kill(static_cast<pid_t>(m_pid), SIGTERM);
        }
        tr().state = RunState::Cancelled;
        finishTarget();
    }

private:
    MatrixRunRecord &rec() { return m_ex->m_records[m_run]; }
    TargetResult &tr() { return rec().targets[m_t]; }
    StepResult &cur() { return tr().steps[m_step]; }
    int log(const QString &t) { return m_ex->appendLogRet(m_run, m_t, t); }

    void adb(const QStringList &args, int timeout, AsyncProcess::Callback cb) {
        m_proc = AsyncProcess::run(this, m_ex->m_emulator->adbPath(), args, timeout, [this, cb](const AsyncProcess::Result &r) {
            m_proc = nullptr;
            if (!m_cancelled) cb(r);
        });
    }
    void later(int ms, std::function<void()> fn) {
        QTimer::singleShot(ms, this, [this, fn] {
            if (!m_cancelled) fn();
        });
    }

    void runStep() {
        if (m_cancelled) return;
        if (m_step >= tr().steps.size()) {
            finishTarget();
            return;
        }
        cur().state = RunState::Running;
        cur().logLine = tr().logLines;
        m_stepTimer.start();
        log(QString("Test Case '-[%1 %2]' started.").arg(tr().avd, cur().name));
        emit m_ex->runChanged(rec().id);
        const QString name = cur().name;
        if (name == "boot") stepBoot();
        else if (name == "abi") stepProp("ro.product.cpu.abi", tr().abi, "ABI");
        else if (name == "api") stepApi();
        else if (name == "logcat") stepLogcat();
        else if (name == "screenshot") stepScreenshot();
        else stepShutdown();
    }

    void endStep(RunState st, const QString &msg, const QString &errorLine = {}) {
        cur().state = st;
        cur().durationMs = m_stepTimer.elapsed();
        cur().message = msg;
        if (st == RunState::Failed) {
            const QString where = QFileInfo::exists(m_ex->m_emulator->avdHome() + "/" + tr().avd + ".avd/config.ini")
                                      ? m_ex->m_emulator->avdHome() + "/" + tr().avd + ".avd/config.ini"
                                      : tr().artifactDir;
            cur().failLine = log(QString("%1: error: -[%2 %3] : %4").arg(errorLine.isEmpty() ? where : errorLine, tr().avd, cur().name, msg));
            log(QString("Test Case '-[%1 %2]' failed (%3 seconds).").arg(tr().avd, cur().name, secs(cur().durationMs)));
        } else if (st == RunState::Skipped) {
            log(QString("Test Case '-[%1 %2]' skipped (%3).").arg(tr().avd, cur().name, msg));
        } else {
            log(QString("Test Case '-[%1 %2]' passed (%3 seconds).").arg(tr().avd, cur().name, secs(cur().durationMs)));
        }
        // No device, no tests: everything between boot and shutdown is skipped.
        if (st == RunState::Failed && cur().name == "boot") {
            for (int i = m_step + 1; i < tr().steps.size(); ++i) {
                if (tr().steps[i].name == "shutdown") continue;
                tr().steps[i].state = RunState::Skipped;
                tr().steps[i].message = "boot failed";
                log(QString("Test Case '-[%1 %2]' skipped (boot failed).").arg(tr().avd, tr().steps[i].name));
            }
        }
        emit m_ex->runChanged(rec().id);
        ++m_step;
        while (m_step < tr().steps.size() && tr().steps[m_step].state == RunState::Skipped) ++m_step;
        later(0, [this] { runStep(); });
    }

    // boot ---------------------------------------------------------------------------------
    void stepBoot() {
        if (!m_ex->m_emulator->info().available) {
            endStep(RunState::Failed, QString("Android emulator or adb not found (emulator: %1, adb: %2). Set ANDROID_HOME to an SDK that has them.")
                                          .arg(m_ex->m_emulator->emulatorPath(), m_ex->m_emulator->adbPath()));
            return;
        }
        m_boot.start();
        m_ex->m_emulator->findSerialAsync(this, tr().avd, [this](const QString &serial) {
            if (m_cancelled) return;
            if (!serial.isEmpty()) {
                tr().serial = serial;
                log("Using the already running emulator " + serial);
                waitBoot();
                return;
            }
            const qint64 pid = m_ex->m_emulator->startAvd(tr().avd, true, tr().artifactDir + "/emulator.log");
            if (pid <= 0) {
                endStep(RunState::Failed, "could not launch the emulator process for " + tr().avd);
                return;
            }
            m_pid = pid;
            tr().startedByUs = true;
            log(QString("Launched emulator for %1 (pid %2)").arg(tr().avd).arg(pid));
            pollSerial();
        });
    }
    void pollSerial() {
        if (m_boot.elapsed() > m_ex->m_bootTimeoutMs) {
            endStep(RunState::Failed, QString("emulator did not register with adb within %1 s").arg(m_ex->m_bootTimeoutMs / 1000));
            return;
        }
        m_ex->m_emulator->findSerialAsync(this, tr().avd, [this](const QString &serial) {
            if (m_cancelled) return;
            if (!serial.isEmpty()) {
                tr().serial = serial;
                log("Emulator attached to adb as " + serial);
                waitBoot();
            } else {
                later(m_ex->m_pollMs, [this] { pollSerial(); });
            }
        });
    }
    void waitBoot() {
        adb({"-s", tr().serial, "shell", "getprop", "sys.boot_completed"}, 8000, [this](const AsyncProcess::Result &r) {
            if (r.ok() && r.text().trimmed() == "1") {
                endStep(RunState::Passed, "booted as " + tr().serial);
            } else if (m_boot.elapsed() > m_ex->m_bootTimeoutMs) {
                endStep(RunState::Failed, QString("sys.boot_completed was not set within %1 s").arg(m_ex->m_bootTimeoutMs / 1000));
            } else {
                later(m_ex->m_pollMs, [this] { waitBoot(); });
            }
        });
    }

    // property checks ----------------------------------------------------------------------
    void stepProp(const QString &prop, const QString &expected, const QString &label) {
        adb({"-s", tr().serial, "shell", "getprop", prop}, 10000, [this, prop, expected, label](const AsyncProcess::Result &r) {
            const QString v = r.text().trimmed();
            if (!r.ok() || v.isEmpty()) endStep(RunState::Failed, QString("could not read %1 from %2").arg(prop, tr().serial));
            else if (v != expected) endStep(RunState::Failed, QString("expected %1 '%2' but the emulator reports '%3'").arg(label, expected, v));
            else endStep(RunState::Passed, QString("%1 = %2").arg(prop, v));
        });
    }
    void stepApi() {
        if (tr().api.isEmpty() || tr().api == "unknown") {
            endStep(RunState::Skipped, "API level unknown in the AVD config");
            return;
        }
        stepProp("ro.build.version.sdk", tr().api, "API level");
    }

    // artifacts ----------------------------------------------------------------------------
    void stepLogcat() {
        adb({"-s", tr().serial, "logcat", "-d", "-v", "threadtime"}, 30000, [this](const AsyncProcess::Result &r) {
            if (!r.ok() || r.out.isEmpty()) {
                endStep(RunState::Failed, "logcat returned no data: " + r.errText().trimmed());
                return;
            }
            QSaveFile f(tr().artifactDir + "/logcat.txt");
            if (!f.open(QIODevice::WriteOnly) || f.write(r.out) != r.out.size() || !f.commit()) {
                endStep(RunState::Failed, "cannot write logcat.txt");
                return;
            }
            endStep(RunState::Passed, QString("%1 lines saved").arg(r.out.count('\n')));
        });
    }
    void stepScreenshot() {
        adb({"-s", tr().serial, "exec-out", "screencap", "-p"}, 20000, [this](const AsyncProcess::Result &r) {
            if (!r.ok() || !r.out.startsWith("\x89PNG")) {
                endStep(RunState::Failed, "screencap returned no PNG image");
                return;
            }
            QSaveFile f(tr().artifactDir + "/screenshot.png");
            if (!f.open(QIODevice::WriteOnly) || f.write(r.out) != r.out.size() || !f.commit()) {
                endStep(RunState::Failed, "cannot write screenshot.png");
                return;
            }
            endStep(RunState::Passed, QString("%1 bytes").arg(r.out.size()));
        });
    }
    void stepShutdown() {
        if (!tr().startedByUs || tr().serial.isEmpty()) {
            endStep(RunState::Skipped, tr().startedByUs ? "no adb serial" : "emulator was already running, left as is");
            return;
        }
        adb({"-s", tr().serial, "emu", "kill"}, 15000, [this](const AsyncProcess::Result &r) {
            if (!r.ok()) {
                endStep(RunState::Failed, "emu kill failed: " + r.errText().trimmed());
                return;
            }
            m_boot.start();
            waitGone();
        });
    }
    void waitGone() {
        m_ex->m_emulator->findSerialAsync(this, tr().avd, [this](const QString &serial) {
            if (m_cancelled) return;
            if (serial.isEmpty()) endStep(RunState::Passed, "emulator stopped");
            else if (m_boot.elapsed() > 30000) endStep(RunState::Failed, "emulator still attached to adb 30 s after emu kill");
            else later(m_ex->m_pollMs, [this] { waitGone(); });
        });
    }

    void finishTarget() {
        if (m_done) return;
        m_done = true;
        auto &t = tr();
        t.finished = QDateTime::currentDateTimeUtc();
        t.durationMs = m_suite.elapsed();
        if (t.state != RunState::Cancelled) t.state = t.failedSteps() > 0 ? RunState::Failed : RunState::Passed;
        const int executed = t.executedSteps(), failed = t.failedSteps();
        log(QString("Test Suite '%1' %2 at %3.").arg(t.avd, t.state == RunState::Passed ? "passed" : (t.state == RunState::Cancelled ? "cancelled" : "failed"), stamp()));
        log(QString("\t Executed %1 tests, with %2 failures (0 unexpected) in %3 (%3) seconds").arg(executed).arg(failed).arg(secs(t.durationMs)));
        emit m_ex->logMessage(QString("matrix: %1 %2").arg(t.avd, t.state == RunState::Passed ? "PASS" : (t.state == RunState::Cancelled ? "CANCELLED" : "FAIL")));
        m_ex->targetDone(m_run, m_t, t.state == RunState::Passed);
    }

    MatrixExecutor *m_ex;
    int m_run, m_t, m_step = 0;
    bool m_started = false, m_done = false, m_cancelled = false;
    qint64 m_pid = -1;
    QElapsedTimer m_stepTimer, m_suite, m_boot;
    QPointer<AsyncProcess> m_proc;
};

// ---------------------------------------------------------------------------------------------

MatrixExecutor::MatrixExecutor(AndroidEmulator *e, ResourceScheduler *s, ArtifactCollector *a, QObject *p)
    : QObject(p), m_emulator(e), m_scheduler(s), m_artifacts(a) {
    bool ok = false;
    const int pm = qEnvironmentVariableIntValue("MOBILELAB_POLL_MS", &ok);
    if (ok && pm >= 20) m_pollMs = pm;
    const int bt = qEnvironmentVariableIntValue("MOBILELAB_BOOT_TIMEOUT_S", &ok);
    if (ok && bt >= 1) m_bootTimeoutMs = bt * 1000;
    loadConfig();
    connect(m_scheduler, &ResourceScheduler::jobStarted, this, [this](const QString &id) {
        for (auto *j : std::as_const(m_jobs))
            if (j->jobId() == id) j->start();
    });
    connect(m_scheduler, &ResourceScheduler::jobCancelled, this, [this](const QString &id) {
        for (auto *j : std::as_const(m_jobs))
            if (!j->done() && j->jobId() == id) j->cancel();
    });
}

MatrixExecutor::~MatrixExecutor() = default;

// Reads the ABI weights of the scheduler from config/matrix/hybrid-x86_64-arm64.yaml.
void MatrixExecutor::loadConfig() {
    QStringList dirs;
    const QString env = qEnvironmentVariable("MOBILELAB_ANDROID_CONFIG");
    if (!env.isEmpty()) dirs << env;
    dirs << QCoreApplication::applicationDirPath() + "/../share/mobilelab-android/config";
#ifdef MOBILELAB_CONFIG_DIR
    dirs << QString(MOBILELAB_CONFIG_DIR);
#endif
    for (const auto &d : dirs) {
        QFile f(d + "/matrix/hybrid-x86_64-arm64.yaml");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        bool inWeights = false;
        for (const auto &raw : QString::fromUtf8(f.readAll()).split('\n')) {
            if (raw.trimmed() == "weights:") { inWeights = true; continue; }
            if (inWeights) {
                if (!raw.startsWith("    ")) { inWeights = false; continue; }
                const int c = raw.indexOf(':');
                bool ok = false;
                const int w = raw.mid(c + 1).trimmed().toInt(&ok);
                if (c > 0 && ok) m_weights.insert(raw.left(c).trimmed(), w);
            }
        }
        if (!m_weights.isEmpty()) {
            m_configSource = QFileInfo(f).absoluteFilePath();
            return;
        }
    }
}

int MatrixExecutor::costForAbi(const QString &abi) const { return m_weights.value(abi, 1); }

QString MatrixExecutor::currentRunId() const { return m_active >= 0 ? m_records[m_active].id : QString(); }

const MatrixRunRecord *MatrixExecutor::record(const QString &id) const {
    for (const auto &r : m_records)
        if (r.id == id) return &r;
    return nullptr;
}

void MatrixExecutor::loadHistory() {
    if (m_active >= 0) return;
    m_records.clear();
    QDir root(m_artifacts->root());
    for (const auto &e : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(e.absoluteFilePath() + "/run.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        auto rec = MatrixRunRecord::fromJson(doc.object(), e.absoluteFilePath());
        if (rec.id.isEmpty()) rec.id = e.fileName();
        if (!rec.started.isValid()) rec.started = e.birthTime().isValid() ? e.birthTime().toUTC() : e.lastModified().toUTC();
        m_records.push_back(rec);
    }
    std::sort(m_records.begin(), m_records.end(), [](const MatrixRunRecord &a, const MatrixRunRecord &b) { return a.started < b.started; });
    emit runChanged(QString());
}

QString MatrixExecutor::logPathFor(const QString &runId, const QString &avd) const {
    const auto *r = record(runId);
    if (!r) return {};
    if (avd.isEmpty()) return r->logPath;
    for (const auto &t : r->targets)
        if (t.avd == avd) return t.artifactDir + "/test.log";
    return {};
}

QStringList MatrixExecutor::readLog(const QString &runId, const QString &avd) const {
    QFile f(logPathFor(runId, avd));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

static void appendToFile(const QString &path, const QString &text) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) f.write((text + "\n").toUtf8());
}

void MatrixExecutor::appendLog(int runIdx, int targetIdx, const QString &text) { appendLogRet(runIdx, targetIdx, text); }

int MatrixExecutor::appendLogRet(int runIdx, int targetIdx, const QString &text) {
    auto &r = m_records[runIdx];
    int lastTargetLine = -1;
    for (const auto &line : text.split('\n')) {
        const int runLine = r.logLines++;
        appendToFile(r.logPath, line);
        emit logLine(r.id, QString(), runLine, line);
        if (targetIdx >= 0) {
            auto &t = r.targets[targetIdx];
            lastTargetLine = t.logLines++;
            appendToFile(t.artifactDir + "/test.log", line);
            emit logLine(r.id, t.avd, lastTargetLine, line);
        } else {
            lastTargetLine = runLine;
        }
    }
    return lastTargetLine;
}

void MatrixExecutor::save(int runIdx) {
    const auto &r = m_records[runIdx];
    const QJsonObject o = r.toJson();
    m_artifacts->writeText(r.dir, "run.json", QJsonDocument(o).toJson(QJsonDocument::Indented));
    m_lastRun = o;
}

QString MatrixExecutor::run(const QVector<MatrixTarget> &targets) {
    if (m_active >= 0 || targets.isEmpty()) return {};
    MatrixRunRecord r;
    r.id = "matrix-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
    r.dir = m_artifacts->beginRun(r.id);
    r.id = QFileInfo(r.dir).fileName();
    r.logPath = r.dir + "/run.log";
    r.started = QDateTime::currentDateTimeUtc();
    r.state = RunState::Running;
    for (const auto &t : targets) {
        TargetResult tr;
        tr.avd = t.avd;
        tr.api = t.api;
        tr.abi = t.abi;
        tr.artifactDir = r.dir + "/" + safeName(t.avd);
        for (const auto &s : kSteps) {
            StepResult sr;
            sr.name = s;
            tr.steps.push_back(sr);
        }
        r.targets.push_back(tr);
    }
    m_records.push_back(r);
    m_active = m_records.size() - 1;
    m_cancelling = false;
    appendLog(m_active, -1, QString("Test Suite 'Matrix %1' started at %2").arg(r.id, stamp()));
    emit logMessage(QString("matrix: run %1 with %2 target(s)").arg(r.id).arg(targets.size()));
    qDeleteAll(m_jobs);
    m_jobs.clear();
    for (int i = 0; i < targets.size(); ++i) {
        auto &tr = m_records[m_active].targets[i];
        tr.jobId = m_scheduler->submitExternal(targets[i].avd, targets[i].command.isEmpty() ? "android-test" : targets[i].command,
                                               costForAbi(targets[i].abi), 50);
        m_jobs.push_back(new TargetJob(this, m_active, i));
    }
    save(m_active);
    emit runStarted(r.id);
    emit runChanged(r.id);
    return r.id;
}

void MatrixExecutor::cancel() {
    if (m_active < 0 || m_cancelling) return;
    m_cancelling = true;
    emit logMessage("matrix: cancelling " + m_records[m_active].id);
    m_records[m_active].state = RunState::Cancelled;
    const auto jobs = m_jobs;
    for (auto *j : jobs) {
        if (j->done()) continue;
        const QString id = j->jobId();
        j->cancel();
        m_scheduler->cancel(id);  // frees the slot; jobCancelled is idempotent on the job
    }
}

void MatrixExecutor::targetDone(int runIdx, int targetIdx, bool ok) {
    auto &t = m_records[runIdx].targets[targetIdx];
    m_scheduler->complete(t.jobId, ok);
    emit targetFinished(t.avd, ok);
    emit runChanged(m_records[runIdx].id);
    save(runIdx);
    bool all = true;
    for (auto *j : std::as_const(m_jobs))
        if (!j->done()) all = false;
    if (all) QTimer::singleShot(0, this, [this, runIdx] { finishRun(runIdx); });
}

void MatrixExecutor::finishRun(int runIdx) {
    if (m_active != runIdx) return;
    auto &r = m_records[runIdx];
    bool anyFailed = false, anyCancelled = false;
    for (const auto &t : r.targets) {
        if (t.state == RunState::Failed) anyFailed = true;
        if (t.state == RunState::Cancelled) anyCancelled = true;
    }
    r.state = anyCancelled ? RunState::Cancelled : (anyFailed ? RunState::Failed : RunState::Passed);
    r.finished = QDateTime::currentDateTimeUtc();
    const int total = r.totalSteps(), failed = r.failedSteps();
    appendLog(runIdx, -1, QString("Test Suite 'Matrix %1' %2 at %3.").arg(r.id, r.state == RunState::Passed ? "passed" : (r.state == RunState::Cancelled ? "cancelled" : "failed"), stamp()));
    appendLog(runIdx, -1, QString("\t Executed %1 tests, with %2 failures (0 unexpected) in %3 (%3) seconds").arg(total).arg(failed).arg(secs(r.durationMs())));
    appendLog(runIdx, -1, r.state == RunState::Passed ? "** TEST SUCCEEDED **" : (r.state == RunState::Cancelled ? "** TEST CANCELLED **" : "** TEST FAILED **"));
    save(runIdx);
    const QString id = r.id;
    for (auto *j : std::as_const(m_jobs)) j->deleteLater();
    m_jobs.clear();
    m_active = -1;
    m_cancelling = false;
    emit logMessage("matrix: artifacts: " + r.dir);
    emit runChanged(id);
    emit runFinished(id);
}

#include "MatrixExecutor.moc"
