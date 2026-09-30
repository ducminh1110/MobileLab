#include "ArtifactCollector.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

ArtifactCollector::ArtifactCollector(const QString &root) {
    m_root = root;
    if (m_root.isEmpty()) m_root = qEnvironmentVariable("MOBILELAB_ANDROID_ARTIFACTS");
    if (m_root.isEmpty()) m_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/artifacts";
    QDir().mkpath(m_root);
}

QString ArtifactCollector::beginRun(const QString &runId) {
    QString safe = runId;
    safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    QString dir = m_root + "/" + safe;
    for (int n = 2; QFileInfo::exists(dir); ++n) dir = m_root + "/" + safe + "-" + QString::number(n);
    QDir().mkpath(dir);
    return dir;
}

bool ArtifactCollector::writeText(const QString &runDir, const QString &name, const QString &text) {
    QDir().mkpath(runDir);
    QSaveFile f(runDir + "/" + name);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return f.commit();
}

bool ArtifactCollector::collectLogcat(const QString &serial, const QString &runDir) {
    QProcess p;
    p.start(m_adb, {"-s", serial, "logcat", "-d", "-v", "threadtime"});
    if (!p.waitForFinished(20000)) return false;
    return writeText(runDir, "logcat.txt", QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()));
}

bool ArtifactCollector::collectScreenshot(const QString &serial, const QString &runDir) {
    QDir().mkpath(runDir);
    QProcess p;
    p.start(m_adb, {"-s", serial, "exec-out", "screencap", "-p"});
    if (!p.waitForFinished(15000)) return false;
    QSaveFile f(runDir + "/screenshot.png");
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(p.readAllStandardOutput());
    return f.commit();
}

qint64 ArtifactCollector::sizeOnDisk() const {
    qint64 total = 0;
    QDirIterator it(m_root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

int ArtifactCollector::cleanOlderThan(int days) {
    int removed = 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-qMax(0, days));
    QDir root(m_root);
    for (const auto &e : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (e.lastModified() <= cutoff && QDir(e.absoluteFilePath()).removeRecursively()) ++removed;
    }
    return removed;
}
