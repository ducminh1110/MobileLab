#include "ResourceScheduler.h"

ResourceScheduler::ResourceScheduler(QObject *p) : QObject(p) {
    m_timer.setParent(this);
    connect(&m_timer, &QTimer::timeout, this, &ResourceScheduler::tick);
    m_timer.start(250);
}

void ResourceScheduler::configure(int cpu, int memoryMb) {
    m_cpu = qMax(1, cpu);
    m_memory = qMax(512, memoryMb);
    emit jobChanged();
}

QString ResourceScheduler::makeId() { return QString("run-%1").arg(++m_seq); }

QString ResourceScheduler::enqueue(const QString &t, const QString &c, int cost) { return submit(t, c, cost, 0, 0); }

QString ResourceScheduler::submit(const QString &t, const QString &c, int cost, int priority, int maxRetries) {
    RunRequest r;
    r.id = makeId();
    r.target = t;
    r.command = c;
    r.cost = qBound(1, cost, qMax(1, capacity()));
    r.priority = qBound(0, priority, 100);
    r.maxRetries = qMax(0, maxRetries);
    m_queue.push_back(r);
    m_logs << QString("queued %1 on %2 (cost=%3 priority=%4 retries=%5)").arg(r.id, t).arg(r.cost).arg(r.priority).arg(r.maxRetries);
    emit logMessage(m_logs.last());
    emit jobChanged();
    return r.id;
}

QString ResourceScheduler::submitExternal(const QString &t, const QString &c, int cost, int priority) {
    RunRequest r;
    r.id = makeId();
    r.target = t;
    r.command = c;
    r.cost = qBound(1, cost, qMax(1, capacity()));
    r.priority = qBound(0, priority, 100);
    r.external = true;
    m_queue.push_back(r);
    m_logs << QString("queued %1 on %2 (cost=%3 priority=%4)").arg(r.id, t).arg(r.cost).arg(r.priority);
    emit logMessage(m_logs.last());
    emit jobChanged();
    return r.id;
}

void ResourceScheduler::complete(const QString &id, bool ok) {
    auto it = m_running.find(id);
    if (it == m_running.end()) return;
    m_used -= it->cost;
    m_running.erase(it);
    emit logMessage(QString(ok ? "completed %1" : "failed %1").arg(id));
    emit jobFinished(id, ok);
    emit jobChanged();
}

void ResourceScheduler::cancel(const QString &id) {
    auto it = m_running.find(id);
    if (it != m_running.end()) {
        m_used -= it->cost;
        m_running.erase(it);
        emit logMessage("cancelled " + id);
        emit jobCancelled(id);
        emit jobChanged();
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == id) {
            m_queue.removeAt(i);
            emit logMessage("cancelled queued " + id);
            emit jobCancelled(id);
            emit jobChanged();
            return;
        }
    }
}

int ResourceScheduler::nextRunnableIndex() const {
    int best = -1;
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_used + m_queue[i].cost > capacity()) continue;
        if (best < 0 || m_queue[i].priority > m_queue[best].priority) best = i;  // FIFO within a priority
    }
    return best;
}

void ResourceScheduler::tick() {
    // Start everything that fits, not just one job per tick.
    for (int guard = 0; guard < 64; ++guard) {
        const int i = nextRunnableIndex();
        if (i < 0) return;
        RunRequest r = m_queue.takeAt(i);
        m_used += r.cost;
        r.attempt++;
        m_running.insert(r.id, r);
        emit logMessage(QString("started %1: %2 (attempt=%3 priority=%4)").arg(r.id, r.command).arg(r.attempt).arg(r.priority));
        if (!r.external) {
            const QString id = r.id;
            QTimer::singleShot(m_leaseMs, this, [this, id] { complete(id, true); });
        }
        emit jobStarted(r.id);
        emit jobChanged();
    }
}

QJsonObject ResourceScheduler::dryRun(const QString &t, const QString &c, int cost, int priority) const {
    const int normalized = qMax(1, cost);
    return {{"target", t}, {"command", c}, {"cost", normalized}, {"priority", qBound(0, priority, 100)},
            {"capacity", capacity()}, {"used_cost", m_used}, {"can_start_now", m_used + normalized <= capacity()},
            {"queued_ahead", m_queue.size()}, {"estimated_slot_ms", m_used + normalized <= capacity() ? 0 : m_leaseMs}};
}

QJsonObject ResourceScheduler::status() const {
    return {{"cpu", m_cpu}, {"memory_mb", m_memory}, {"queued", m_queue.size()}, {"running", m_running.size()},
            {"used_cost", m_used}, {"capacity", capacity()}, {"features", "priority,retry,dry-run"}};
}
