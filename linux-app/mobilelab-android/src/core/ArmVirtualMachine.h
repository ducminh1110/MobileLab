#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>

struct VmSpec {
    QString name, kernel, initrd, disk;
    int memoryMb = 4096;
    int cpus = 2;
    bool acceleration = true;
};

class ArmVirtualMachine : public QObject {
    Q_OBJECT
public:
    explicit ArmVirtualMachine(QObject *p = nullptr);
    bool probe();
    bool start(const VmSpec &spec);
    void stop();
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool kvm() const { return m_kvm; }
    bool qemu() const { return m_qemu; }
    QJsonObject status() const;
signals:
    void logMessage(const QString &message);
private:
    QString m_qemuPath;
    bool m_kvm = false;
    bool m_qemu = false;
    QProcess m_process;
};
