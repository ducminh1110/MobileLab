#pragma once
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include "MatrixRun.h"

struct MatrixTarget { QString avd; QString api; QString abi = "arm64-v8a"; QString command; };

class AndroidEmulator;
class ResourceScheduler;
class ArtifactCollector;
class TargetJob;

// Runs a set of AVDs as one matrix run. Every target is a suite whose steps (boot, abi, api,
// logcat, screenshot, shutdown) are test cases with real pass/fail results. Targets are admitted
// through the ResourceScheduler, everything is asynchronous, artifacts land in the run directory.
class MatrixExecutor final : public QObject {
    Q_OBJECT
public:
    MatrixExecutor(AndroidEmulator *emulator, ResourceScheduler *scheduler, ArtifactCollector *artifacts, QObject *parent = nullptr);
    ~MatrixExecutor() override;
    QString run(const QVector<MatrixTarget> &targets);  // returns the run id, empty if nothing was started
    void cancel();
    bool isRunning() const { return m_active >= 0; }
    QString currentRunId() const;
    QJsonObject lastRun() const { return m_lastRun; }
    const QVector<MatrixRunRecord> &records() const { return m_records; }
    const MatrixRunRecord *record(const QString &id) const;
    void loadHistory();
    QStringList readLog(const QString &runId, const QString &avd = {}) const;
    QString logPathFor(const QString &runId, const QString &avd = {}) const;
    int pollMs() const { return m_pollMs; }
    int bootTimeoutMs() const { return m_bootTimeoutMs; }
    // Weight of an ABI in scheduler cost units (from config/matrix/*.yaml when found).
    int costForAbi(const QString &abi) const;
    QString configSource() const { return m_configSource; }

signals:
    void logMessage(const QString &);
    void targetFinished(const QString &, bool);
    void runStarted(const QString &id);
    void runChanged(const QString &id);
    void runFinished(const QString &id);
    // A line was appended: avd is empty for run level lines.
    void logLine(const QString &runId, const QString &avd, int line, const QString &text);

private:
    friend class TargetJob;
    void appendLog(int runIdx, int targetIdx, const QString &text);
    int appendLogRet(int runIdx, int targetIdx, const QString &text);
    void targetDone(int runIdx, int targetIdx, bool ok);
    void finishRun(int runIdx);
    void save(int runIdx);
    void loadConfig();
    AndroidEmulator *m_emulator;
    ResourceScheduler *m_scheduler;
    ArtifactCollector *m_artifacts;
    QVector<MatrixRunRecord> m_records;
    QVector<TargetJob *> m_jobs;
    QJsonObject m_lastRun;
    int m_active = -1;
    bool m_cancelling = false;
    int m_pollMs = 1000;
    int m_bootTimeoutMs = 240000;
    QHash<QString, int> m_weights;
    QString m_configSource;
};
