#pragma once
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

struct AndroidAvd {
    QString name;
    QString api;      // API level ("35") or "unknown"
    QString abi;      // e.g. x86_64, arm64-v8a
    QString tag;      // system image tag, e.g. google_apis
    QString device;   // hardware profile, e.g. pixel_8
    QString path;     // AVD directory
    QString displayName;
    bool running = false;
};

struct AndroidEmulatorInfo {
    QString sdkRoot;
    QString emulatorPath;
    QString adbPath;
    QString cmdlineToolsPath;
    bool available = false;
};

// Thin wrapper over the Android SDK command line tools. Discovery honours ANDROID_HOME,
// ANDROID_SDK_ROOT, ANDROID_AVD_HOME and ANDROID_USER_HOME, then falls back to PATH.
class AndroidEmulator : public QObject {
    Q_OBJECT
public:
    explicit AndroidEmulator(QObject *parent = nullptr);
    bool discover();
    bool createAvd(const QString &name, const QString &packageName, const QString &device);
    // Starts the emulator detached; returns its pid or -1. Output goes to logFile when given.
    qint64 startAvd(const QString &name, bool noWindow, const QString &logFile = {});
    bool start(const QString &name, bool noWindow = true);
    bool stop(const QString &name);
    bool stopSerial(const QString &serial);
    bool adb(const QStringList &args, QString *output = nullptr);
    QStringList installedSystemImages() const;
    AndroidEmulatorInfo info() const;
    QVector<AndroidAvd> avds() const { return m_avds; }
    QString sdkRoot() const { return m_sdkRoot; }
    QString emulatorPath() const { return m_emulator; }
    QString adbPath() const { return m_adb; }
    QString avdManagerPath() const { return m_avdManager; }
    QString avdHome() const;
    // Blocking helpers (used by the REST server thread-free paths and tests).
    QString serialFor(const QString &avdName);
    // Asynchronous: finds the adb serial of the running emulator for an AVD (empty when none).
    void findSerialAsync(QObject *owner, const QString &avdName, std::function<void(const QString &)> cb);

    static QMap<QString, QString> parseIni(const QString &text);
    static QString normaliseAbi(const QString &raw);
    static QString apiFromSysdir(const QString &sysdirOrTarget);

private:
    QString m_sdkRoot, m_emulator, m_adb, m_avdManager;
    QVector<AndroidAvd> m_avds;
    bool run(const QString &program, const QStringList &args, QString *output = nullptr, int timeout = 10000) const;
    QStringList listAvdNames(bool *usedTool) const;
};
