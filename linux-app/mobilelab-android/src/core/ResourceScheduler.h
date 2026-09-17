#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVector>

struct RunRequest {
    QString id;
    int cost = 1;
    int priority = 0;
    int maxRetries = 0;
    int attempt = 0;
    QString target;
    QString command;
};

// Executes local, explicitly submitted commands while enforcing a weighted
// concurrency budget. Commands are only accepted from the loopback API.
class ResourceScheduler : public QObject {
    Q_OBJECT
public:
    explicit ResourceScheduler(QObject *parent = nullptr);

    void configure(int cpu, int memoryMb);
    QString enqueue(const QString &target, const QString &command, int cost = 1);
    QString submit(const QString &target, const QString &command, int cost,
                   int priority, int maxRetries);
    void cancel(const QString &id);
    QJsonObject dryRun(const QString &target, const QString &command, int cost,
                       int priority = 0) const;
    QJsonObject status() const;
    QStringList logs() const { return m_logs; }

signals:
    void logMessage(const QString &message);
    void jobChanged();

private slots:
    void tick();

private:
    int capacity() const { return m_cpu * 2; }
    int nextRunnableIndex() const;
    void start(RunRequest request);
    void finish(const QString &id, int exitCode, QProcess::ExitStatus exitStatus);
    void appendLog(const QString &message);

    int m_cpu = 1;
    int m_memory = 2048;
    int m_used = 0;
    quint64 m_sequence = 0;
    QVector<RunRequest> m_queue;
    QHash<QString, RunRequest> m_running;
    QHash<QString, QProcess *> m_processes;
    QSet<QString> m_cancelled;
    QStringList m_logs;
    QTimer m_timer;
};
