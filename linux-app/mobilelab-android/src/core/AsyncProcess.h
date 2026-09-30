#pragma once
// Fire-and-forget asynchronous process with a timeout. Owned by `owner`, deletes itself when done.
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

class QTimer;

class AsyncProcess final : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool started = false;
        bool timedOut = false;
        bool crashed = false;
        int exitCode = -1;
        QByteArray out;
        QByteArray err;
        bool ok() const { return started && !timedOut && !crashed && exitCode == 0; }
        QString text() const { return QString::fromLocal8Bit(out); }
        QString errText() const { return QString::fromLocal8Bit(err); }
    };
    using Callback = std::function<void(const Result &)>;

    // The callback runs on the owner's thread. It is not called if the process was aborted.
    // `input` is written to stdin and the channel is closed (used to answer avdmanager's prompt).
    static AsyncProcess *run(QObject *owner, const QString &program, const QStringList &args,
                             int timeoutMs, Callback cb, const QByteArray &input = {});
    void abort();
    ~AsyncProcess() override;

private:
    AsyncProcess(QObject *owner, Callback cb);
    void finish(bool timedOut);
    QProcess *m_proc = nullptr;
    QTimer *m_timer = nullptr;
    Callback m_cb;
    bool m_done = false;
    bool m_started = false;
};
