#include "HostMetrics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QThread>
#include <unistd.h>

namespace {
QByteArray slurp(const char *path) {
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();  // /proc files report size 0, readAll handles it
}
}

HostMetrics::HostMetrics(QObject *parent) : QObject(parent) {
    m_pid = QCoreApplication::applicationPid();
    m_cores = qMax(1, QThread::idealThreadCount());
}

qint64 HostMetrics::parseKb(const QString &line) {
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    return parts.size() >= 2 ? parts[1].toLongLong() * 1024 : 0;
}

void HostMetrics::sample() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_lastWall ? (now - m_lastWall) / 1000.0 : 0;

    // process CPU ticks: utime + stime (fields 14 and 15 after the ")" of the command name)
    quint64 ticks = 0;
    {
        const QByteArray stat = slurp("/proc/self/stat");
        const int rp = stat.lastIndexOf(')');
        const auto f = QString::fromLatin1(stat.mid(rp + 2)).split(' ');
        if (f.size() > 13) ticks = f[11].toULongLong() + f[12].toULongLong();  // index 0 is field 3 (state)
    }
    const double hz = double(sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100);
    if (dt > 0 && m_lastProcTicks) m_procCpu = qMax(0.0, (ticks - m_lastProcTicks) / hz / dt * 100.0);
    m_lastProcTicks = ticks;

    // host CPU
    {
        const auto line = QString::fromLatin1(slurp("/proc/stat")).section('\n', 0, 0).split(' ', Qt::SkipEmptyParts);
        if (line.size() >= 8) {
            quint64 total = 0;
            for (int i = 1; i < line.size() && i <= 8; ++i) total += line[i].toULongLong();
            const quint64 idle = line[4].toULongLong() + line[5].toULongLong();
            if (m_lastHostTotal && total > m_lastHostTotal)
                m_hostCpu = 100.0 * double((total - m_lastHostTotal) - (idle - m_lastHostIdle)) / double(total - m_lastHostTotal);
            m_lastHostTotal = total;
            m_lastHostIdle = idle;
        }
    }
    // memory
    for (const auto &l : QString::fromLatin1(slurp("/proc/self/status")).split('\n'))
        if (l.startsWith("VmRSS:")) m_rss = parseKb(l);
    for (const auto &l : QString::fromLatin1(slurp("/proc/meminfo")).split('\n')) {
        if (l.startsWith("MemTotal:")) m_memTotal = parseKb(l);
        else if (l.startsWith("MemAvailable:")) m_memAvail = parseKb(l);
    }
    // disk I/O of this process
    {
        const QByteArray io = slurp("/proc/self/io");
        m_diskOk = !io.isEmpty();
        quint64 bytes = 0;
        for (const auto &l : QString::fromLatin1(io).split('\n'))
            if (l.startsWith("read_bytes:") || l.startsWith("write_bytes:")) bytes += l.section(':', 1).trimmed().toULongLong();
        if (m_diskOk && dt > 0 && m_lastDiskBytes) m_diskBps = bytes >= m_lastDiskBytes ? (bytes - m_lastDiskBytes) / dt : 0;
        m_lastDiskBytes = bytes;
    }
    // network: every interface but lo
    {
        const QByteArray net = slurp("/proc/net/dev");
        m_netOk = !net.isEmpty();
        quint64 bytes = 0;
        const auto lines = QString::fromLatin1(net).split('\n');
        for (int i = 2; i < lines.size(); ++i) {
            const QString l = lines[i].trimmed();
            if (l.isEmpty() || l.startsWith("lo:")) continue;
            const auto f = l.section(':', 1).split(' ', Qt::SkipEmptyParts);
            if (f.size() >= 9) bytes += f[0].toULongLong() + f[8].toULongLong();
        }
        if (m_netOk && dt > 0 && m_lastNetBytes) m_netBps = bytes >= m_lastNetBytes ? (bytes - m_lastNetBytes) / dt : 0;
        m_lastNetBytes = bytes;
    }
    m_load1 = QString::fromLatin1(slurp("/proc/loadavg")).section(' ', 0, 0).toDouble();
    m_lastWall = now;
    ++m_samples;
    emit updated();
}

QString HostMetrics::formatBytes(double b) {
    if (b < 1024) return QString::number(qint64(b)) + " B";
    if (b < 1024 * 1024) return QString::number(b / 1024, 'f', b < 10 * 1024 ? 1 : 0) + " KB";
    if (b < 1024.0 * 1024 * 1024) return QString::number(b / 1024 / 1024, 'f', 1) + " MB";
    return QString::number(b / 1024 / 1024 / 1024, 'f', 2) + " GB";
}

QString HostMetrics::formatRate(double b) {
    if (b < 1) return "Zero KB/s";
    if (b < 1024 * 1024) return QString::number(b / 1024, 'f', 1) + " KB/s";
    return QString::number(b / 1024 / 1024, 'f', 1) + " MB/s";
}
