#pragma once
#include <QString>
#include <QStringList>

class ArtifactCollector {
public:
    // root: explicit directory, else $MOBILELAB_ANDROID_ARTIFACTS, else <AppLocalData>/artifacts.
    explicit ArtifactCollector(const QString &root = {});
    void setAdb(const QString &adbPath) { m_adb = adbPath; }
    QString beginRun(const QString &runId);
    bool collectLogcat(const QString &serial, const QString &runDir);
    bool collectScreenshot(const QString &serial, const QString &runDir);
    bool writeText(const QString &runDir, const QString &name, const QString &text);
    QString root() const { return m_root; }
    // Total size in bytes of everything below the root (blocking, cheap for typical artifact counts).
    qint64 sizeOnDisk() const;
    // Deletes run directories older than `days`; returns the number removed.
    int cleanOlderThan(int days);

private:
    QString m_root;
    QString m_adb = "adb";
};
