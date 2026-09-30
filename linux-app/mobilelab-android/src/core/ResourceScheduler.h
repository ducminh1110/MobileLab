#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
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
    // External jobs are driven by their owner (for example the matrix executor): the scheduler
    // only grants the slot (jobStarted) and takes it back on complete()/cancel().
    bool external = false;
};

// Slot accountant with priorities. Capacity is `2 * cpu` cost units.
class ResourceScheduler : public QObject {
    Q_OBJECT
public:
    explicit ResourceScheduler(QObject *p = nullptr);
    void configure(int cpu, int memoryMb);
    QString enqueue(const QString &target, const QString &command, int cost = 1);
    QString submit(const QString &target, const QString &command, int cost, int priority, int maxRetries);
    QString submitExternal(const QString &target, const QString &command, int cost, int priority);
    void complete(const QString &id, bool ok);
    void cancel(const QString &id);
    QJsonObject dryRun(const QString &target, const QString &command, int cost, int priority = 0) const;
    QJsonObject status() const;
    QStringList logs() const { return m_logs; }
    QVector<RunRequest> runningJobs() const { return m_running.values().toVector(); }
    QVector<RunRequest> queuedJobs() const { return m_queue; }
    int capacity() const { return m_cpu * 2; }
    int usedCost() const { return m_used; }
    int cpuSlots() const { return m_cpu; }
    int memoryMb() const { return m_memory; }
    // Slot lease for non-external (API submitted) jobs, which have no executor of their own.
    void setLeaseMs(int ms) { m_leaseMs = qMax(50, ms); }

signals:
    void logMessage(const QString &);
    void jobChanged();
    void jobStarted(const QString &id);
    void jobFinished(const QString &id, bool ok);
    void jobCancelled(const QString &id);

private slots:
    void tick();

private:
    int nextRunnableIndex() const;
    QString makeId();
    int m_cpu = 1, m_memory = 2048, m_used = 0, m_leaseMs = 1500;
    quint64 m_seq = 0;
    QVector<RunRequest> m_queue;
    QHash<QString, RunRequest> m_running;
    QStringList m_logs;
    QTimer m_timer;
};
