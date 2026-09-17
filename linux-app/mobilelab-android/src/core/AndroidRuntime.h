#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QJsonObject>

class AndroidEmulator;

struct AndroidTarget {
    QString id;
    QString api;
    QString arch;
    QString state="stopped";
    QString backend="emulator";
    QString stability="experimental";
    QStringList tags;
    int healthScore=50;
    int pid=-1;
};

class AndroidRuntime : public QObject {
    Q_OBJECT
public:
    explicit AndroidRuntime(QObject *parent=nullptr);
    void setEmulator(AndroidEmulator *emulator);
    bool probe();
    void refreshTargets();
    bool start(const QString &id);
    bool stop(const QString &id);
    bool restart(const QString &id);
    bool shell(const QString &id,const QString &command);
    bool screenshot(const QString &id,const QString &path);
    QJsonObject status() const;
    bool kvmAvailable() const{return m_kvm;}
    bool qemuAvailable() const{return m_qemu;}
    bool emulatorAvailable() const{return m_emulator;}
    bool x86_64Host() const{return m_x86_64Host;}
    bool arm64Host() const{return m_arm64Host;}
    bool hybridAbiAvailable() const{return m_hasArm64Abi && m_hasX8664Abi;}
    QString architecture() const{return m_arch;}
    QString kernel() const{return m_kernel;}
    QStringList supportedAbis() const{return m_supportedAbis;}
    QList<AndroidTarget> targets() const{return m_targets;}
    // Resolves an explicit target or picks the healthiest compatible target.
    QString resolveTarget(const QString &requested = {}, const QString &abi = {}) const;
signals:
    void logMessage(const QString &message); void targetChanged(const QString &id);
private:
    int indexOf(const QString &id) const;
    QString findExecutable(const QStringList &names) const;
    QString classifyTargetStability(const QString &abi) const;
    QStringList deriveTags(const AndroidTarget &target) const;
    int calculateHealthScore(const AndroidTarget &target) const;
    QList<AndroidTarget> m_targets;
    AndroidEmulator *m_androidEmulator=nullptr;
    QString m_arch,m_kernel;
    QStringList m_supportedAbis;
    bool m_kvm=false,m_qemu=false,m_emulator=false,m_arm64Host=false,m_x86_64Host=false,m_hasArm64Abi=false,m_hasX8664Abi=false;
};
