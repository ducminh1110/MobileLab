#pragma once
// Real host and process numbers for the Debug navigator, read from /proc. No synthetic values:
// anything that cannot be read is reported as unavailable.
#include <QObject>
#include <QString>

class HostMetrics : public QObject {
    Q_OBJECT
public:
    explicit HostMetrics(QObject *parent = nullptr);
    void sample();  // call periodically (the Debug navigator does, every 2 s while visible)

    qint64 pid() const { return m_pid; }
    int cores() const { return m_cores; }
    // Process CPU in percent of one core (can exceed 100 with several threads).
    double processCpuPercent() const { return m_procCpu; }
    double hostCpuPercent() const { return m_hostCpu; }
    qint64 rssBytes() const { return m_rss; }
    qint64 hostMemTotal() const { return m_memTotal; }
    qint64 hostMemAvailable() const { return m_memAvail; }
    double diskBytesPerSec() const { return m_diskBps; }     // read + write of this process
    bool diskAvailable() const { return m_diskOk; }
    double netBytesPerSec() const { return m_netBps; }       // all non loopback interfaces
    bool netAvailable() const { return m_netOk; }
    double load1() const { return m_load1; }
    bool valid() const { return m_samples >= 2; }

    static QString formatBytes(double bytes);
    static QString formatRate(double bytesPerSec);
    static qint64 parseKb(const QString &line);  // "VmRSS:  1234 kB" -> bytes

signals:
    void updated();

private:
    qint64 m_pid = 0;
    int m_cores = 1;
    double m_procCpu = 0, m_hostCpu = 0, m_diskBps = 0, m_netBps = 0, m_load1 = 0;
    qint64 m_rss = 0, m_memTotal = 0, m_memAvail = 0;
    bool m_diskOk = false, m_netOk = false;
    int m_samples = 0;
    // previous readings
    qint64 m_lastWall = 0;
    quint64 m_lastProcTicks = 0, m_lastHostTotal = 0, m_lastHostIdle = 0, m_lastDiskBytes = 0, m_lastNetBytes = 0;
};
