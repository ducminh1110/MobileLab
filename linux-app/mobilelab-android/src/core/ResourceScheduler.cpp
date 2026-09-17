#include "ResourceScheduler.h"

namespace {
constexpr int kMaxPriority = 100;
constexpr int kMaxRetries = 10;
constexpr int kMaxLogEntries = 500;
}

ResourceScheduler::ResourceScheduler(QObject *parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &ResourceScheduler::tick);
    m_timer.start(100);
}

void ResourceScheduler::configure(int cpu, int memoryMb) {
    m_cpu = qMax(1, cpu);
    m_memory = qMax(512, memoryMb);
    emit jobChanged();
}

QString ResourceScheduler::enqueue(const QString &target, const QString &command, int cost) {
    return submit(target, command, cost, 0, 0);
}

QString ResourceScheduler::submit(const QString &target, const QString &command, int cost,
                                  int priority, int maxRetries) {
    RunRequest request;
    request.id = QStringLiteral("run-%1").arg(++m_sequence);
    request.target = target.trimmed();
    request.command = command.trimmed();
    request.cost = qBound(1, cost, capacity());
    request.priority = qBound(0, priority, kMaxPriority);
    request.maxRetries = qBound(0, maxRetries, kMaxRetries);
    m_queue.push_back(request);
    appendLog(QStringLiteral("queued %1 on %2 (cost=%3 priority=%4 retries=%5)")
                  .arg(request.id, request.target)
                  .arg(request.cost)
                  .arg(request.priority)
                  .arg(request.maxRetries));
    emit jobChanged();
    return request.id;
}

void ResourceScheduler::cancel(const QString &id) {
    if (QProcess *process = m_processes.value(id, nullptr)) {
        m_cancelled.insert(id);
        appendLog(QStringLiteral("cancelling %1").arg(id));
        process->kill();
        return;
    }

    for (int index = 0; index < m_queue.size(); ++index) {
        if (m_queue.at(index).id == id) {
            m_queue.removeAt(index);
            appendLog(QStringLiteral("cancelled queued %1").arg(id));
            emit jobChanged();
            return;
        }
    }
}

int ResourceScheduler::nextRunnableIndex() const {
    int best = -1;
    for (int index = 0; index < m_queue.size(); ++index) {
        const RunRequest &candidate = m_queue.at(index);
        if (m_used + candidate.cost > capacity()) {
            continue;
        }
        if (best < 0 || candidate.priority > m_queue.at(best).priority ||
            (candidate.priority == m_queue.at(best).priority && candidate.id < m_queue.at(best).id)) {
            best = index;
        }
    }
    return best;
}

void ResourceScheduler::tick() {
    const int index = nextRunnableIndex();
    if (index >= 0) {
        start(m_queue.takeAt(index));
    }
}

void ResourceScheduler::start(RunRequest request) {
    ++request.attempt;
    m_used += request.cost;
    m_running.insert(request.id, request);

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_processes.insert(request.id, process);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, id = request.id](int exitCode, QProcess::ExitStatus exitStatus) {
                finish(id, exitCode, exitStatus);
            });
    connect(process, &QProcess::errorOccurred, this, [this, id = request.id](QProcess::ProcessError) {
        // FailedToStart does not emit finished on every supported Qt platform.
        if (m_processes.contains(id) && m_processes.value(id)->state() == QProcess::NotRunning) {
            finish(id, -1, QProcess::CrashExit);
        }
    });

    appendLog(QStringLiteral("started %1: %2 (attempt=%3 priority=%4)")
                  .arg(request.id, request.command)
                  .arg(request.attempt)
                  .arg(request.priority));
    process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), request.command});
    emit jobChanged();
}

void ResourceScheduler::finish(const QString &id, int exitCode, QProcess::ExitStatus exitStatus) {
    QProcess *process = m_processes.take(id);
    if (process) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        if (!output.isEmpty()) {
            appendLog(QStringLiteral("%1 output: %2").arg(id, output.left(1024)));
        }
        process->deleteLater();
    }

    const RunRequest request = m_running.take(id);
    if (request.id.isEmpty()) {
        return;
    }
    m_used = qMax(0, m_used - request.cost);

    const bool cancelled = m_cancelled.remove(id);
    const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
    if (cancelled) {
        appendLog(QStringLiteral("cancelled %1").arg(id));
    } else if (!succeeded && request.attempt <= request.maxRetries) {
        RunRequest retry = request;
        m_queue.push_back(retry);
        appendLog(QStringLiteral("retrying %1 after attempt %2/%3")
                      .arg(id)
                      .arg(request.attempt)
                      .arg(request.maxRetries + 1));
    } else if (succeeded) {
        appendLog(QStringLiteral("completed %1").arg(id));
    } else {
        appendLog(QStringLiteral("failed %1 (exit=%2, attempts=%3)")
                      .arg(id)
                      .arg(exitCode)
                      .arg(request.attempt));
    }
    emit jobChanged();
}

void ResourceScheduler::appendLog(const QString &message) {
    m_logs.append(message);
    if (m_logs.size() > kMaxLogEntries) {
        m_logs.remove(0, m_logs.size() - kMaxLogEntries);
    }
    emit logMessage(message);
}

QJsonObject ResourceScheduler::dryRun(const QString &target, const QString &command, int cost,
                                      int priority) const {
    const int normalizedCost = qBound(1, cost, capacity());
    return {{QStringLiteral("target"), target.trimmed()},
            {QStringLiteral("command"), command.trimmed()},
            {QStringLiteral("cost"), normalizedCost},
            {QStringLiteral("priority"), qBound(0, priority, kMaxPriority)},
            {QStringLiteral("capacity"), capacity()},
            {QStringLiteral("used_cost"), m_used},
            {QStringLiteral("can_start_now"), m_used + normalizedCost <= capacity()},
            {QStringLiteral("queued_ahead"), m_queue.size()},
            {QStringLiteral("estimated_slot_ms"), m_used + normalizedCost <= capacity() ? 0 : 100}};
}

QJsonObject ResourceScheduler::status() const {
    return {{QStringLiteral("cpu"), m_cpu},
            {QStringLiteral("memory_mb"), m_memory},
            {QStringLiteral("queued"), m_queue.size()},
            {QStringLiteral("running"), m_running.size()},
            {QStringLiteral("used_cost"), m_used},
            {QStringLiteral("capacity"), capacity()},
            {QStringLiteral("features"), QStringLiteral("priority,retry,dry-run,process-execution")}};
}
