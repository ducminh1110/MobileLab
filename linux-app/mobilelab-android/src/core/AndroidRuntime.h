#pragma once
#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QSharedPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <functional>

class AndroidEmulator;

struct AndroidTarget {
    QString id;
    QString api;
    QString arch;
    QString state = "stopped";       // stopped | booting | running | stopping
    QString backend = "google-emulator";
    QString stability = "experimental";
    QString tag;                      // system image tag
    QString device;                   // hardware profile
    QString serial;                   // adb serial while running, e.g. emulator-5554
    QStringList tags;
    int healthScore = 50;             // heuristic, see AndroidRuntime::calculateHealthScore
    qint64 pid = -1;                  // emulator process started by this app, if any
};

class AndroidRuntime : public QObject {
    Q_OBJECT
public:
    explicit AndroidRuntime(QObject *parent = nullptr);
    void setEmulator(AndroidEmulator *emulator);
    bool probe();
    void refreshTargets();          // blocking discovery (AVD list from the SDK)
    void refreshStates();           // asynchronous: which AVDs are running right now
    bool start(const QString &id);  // dispatches the emulator, state becomes "booting"
    bool stop(const QString &id);
    bool restart(const QString &id);
    bool shell(const QString &id, const QString &command);  // asynchronous, output goes to logMessage
    bool screenshot(const QString &id, const QString &path); // blocking
    void screenshotAsync(const QString &id, const QString &path, std::function<void(bool, const QString &)> done);
    QJsonObject status() const;
    bool kvmAvailable() const { return m_kvm; }
    bool qemuAvailable() const { return m_qemu; }
    bool emulatorAvailable() const { return m_emulator; }
    bool x86_64Host() const { return m_x86_64Host; }
    bool arm64Host() const { return m_arm64Host; }
    bool hybridAbiAvailable() const { return m_hasArm64Abi && m_hasX8664Abi; }
    QString architecture() const { return m_arch; }
    QString kernel() const { return m_kernel; }
    QStringList supportedAbis() const { return m_supportedAbis; }
    QList<AndroidTarget> targets() const { return m_targets; }
    const AndroidTarget *target(const QString &id) const;
    int runningCount() const;

signals:
    void logMessage(const QString &message);
    void targetChanged(const QString &id);
    void targetsChanged();

private:
    int indexOf(const QString &id) const;
    QString findExecutable(const QStringList &names) const;
    QString classifyTargetStability(const QString &abi) const;
    QStringList deriveTags(const AndroidTarget &target) const;
    int calculateHealthScore(const AndroidTarget &target) const;
    void schedulePoll();
    void finishStateProbe(const QHash<QString, QString> &map, const QStringList &bootedSerials);
    QList<AndroidTarget> m_targets;
    AndroidEmulator *m_androidEmulator = nullptr;
    QTimer m_pollTimer;
    bool m_probing = false;
    QString m_arch, m_kernel;
    QStringList m_supportedAbis;
    bool m_kvm = false, m_qemu = false, m_emulator = false, m_arm64Host = false, m_x86_64Host = false,
         m_hasArm64Abi = false, m_hasX8664Abi = false;
};
