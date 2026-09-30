#include "AsyncProcess.h"
#include <QTimer>

AsyncProcess::AsyncProcess(QObject *owner, Callback cb) : QObject(owner), m_cb(std::move(cb)) {
    m_proc = new QProcess(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
}

AsyncProcess *AsyncProcess::run(QObject *owner, const QString &program, const QStringList &args,
                                int timeoutMs, Callback cb) {
    auto *p = new AsyncProcess(owner, std::move(cb));
    connect(p->m_proc, &QProcess::started, p, [p] { p->m_started = true; });
    connect(p->m_proc, &QProcess::errorOccurred, p, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) p->finish(false);
    });
    connect(p->m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
            [p](int, QProcess::ExitStatus) { p->finish(false); });
    connect(p->m_timer, &QTimer::timeout, p, [p] { p->finish(true); });
    p->m_timer->start(qMax(100, timeoutMs));
    p->m_proc->start(program, args);
    return p;
}

void AsyncProcess::finish(bool timedOut) {
    if (m_done) return;
    m_done = true;
    m_timer->stop();
    Result r;
    r.started = m_started;
    r.timedOut = timedOut;
    if (timedOut && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
    r.crashed = !timedOut && m_started && m_proc->exitStatus() == QProcess::CrashExit;
    r.exitCode = m_started ? m_proc->exitCode() : -1;
    r.out = m_proc->readAllStandardOutput();
    r.err = m_proc->readAllStandardError();
    Callback cb = std::move(m_cb);
    m_cb = nullptr;
    deleteLater();
    if (cb) cb(r);
}

void AsyncProcess::abort() {
    if (m_done) return;
    m_done = true;
    m_cb = nullptr;
    m_timer->stop();
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
    deleteLater();
}

AsyncProcess::~AsyncProcess() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect();
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}
