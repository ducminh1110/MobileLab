#include "AndroidEmulator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSharedPointer>
#include "AsyncProcess.h"

namespace {
QString firstExisting(const QStringList &candidates) {
    for (const auto &c : candidates)
        if (QFileInfo::exists(c)) return c;
    return {};
}
}

AndroidEmulator::AndroidEmulator(QObject *parent) : QObject(parent) {
    m_sdkRoot = qEnvironmentVariable("ANDROID_HOME");
    if (m_sdkRoot.isEmpty()) m_sdkRoot = qEnvironmentVariable("ANDROID_SDK_ROOT");
    if (m_sdkRoot.isEmpty()) m_sdkRoot = QDir::homePath() + "/Android/Sdk";
    m_emulator = m_sdkRoot + "/emulator/emulator";
    m_adb = m_sdkRoot + "/platform-tools/adb";
    m_avdManager = firstExisting({m_sdkRoot + "/cmdline-tools/latest/bin/avdmanager",
                                  m_sdkRoot + "/cmdline-tools/bin/avdmanager",
                                  m_sdkRoot + "/tools/bin/avdmanager"});
    if (m_avdManager.isEmpty()) m_avdManager = m_sdkRoot + "/cmdline-tools/latest/bin/avdmanager";
    // PATH fallback so a system-wide adb/emulator is still found.
    if (!QFileInfo::exists(m_emulator)) {
        const auto p = QStandardPaths::findExecutable("emulator");
        if (!p.isEmpty()) m_emulator = p;
    }
    if (!QFileInfo::exists(m_adb)) {
        const auto p = QStandardPaths::findExecutable("adb");
        if (!p.isEmpty()) m_adb = p;
    }
    if (!QFileInfo::exists(m_avdManager)) {
        const auto p = QStandardPaths::findExecutable("avdmanager");
        if (!p.isEmpty()) m_avdManager = p;
    }
}

QString AndroidEmulator::avdHome() const {
    QString h = qEnvironmentVariable("ANDROID_AVD_HOME");
    if (!h.isEmpty()) return h;
    h = qEnvironmentVariable("ANDROID_USER_HOME");
    if (!h.isEmpty()) return h + "/avd";
    return QDir::homePath() + "/.android/avd";
}

bool AndroidEmulator::run(const QString &program, const QStringList &args, QString *output, int timeout) const {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000)) return false;
    if (!p.waitForFinished(timeout)) {
        p.kill();
        p.waitForFinished(500);
        return false;
    }
    if (output) *output = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QMap<QString, QString> AndroidEmulator::parseIni(const QString &text) {
    QMap<QString, QString> m;
    for (const auto &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        m.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return m;
}

QString AndroidEmulator::normaliseAbi(const QString &raw) {
    const QString r = raw.trimmed();
    if (r == "arm64" || r == "aarch64") return "arm64-v8a";
    if (r == "arm") return "armeabi-v7a";
    if (r == "x86_64" || r == "x86" || r.startsWith("arm")) return r;
    return r;
}

QString AndroidEmulator::apiFromSysdir(const QString &s) {
    static const QRegularExpression re("android-(\\d+)");
    const auto m = re.match(s);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList AndroidEmulator::listAvdNames(bool *usedTool) const {
    QStringList names;
    QString out;
    if (QFileInfo::exists(m_emulator) && run(m_emulator, {"-list-avds"}, &out)) {
        for (const auto &l : out.split('\n', Qt::SkipEmptyParts)) {
            const QString n = l.trimmed();
            // The real emulator may print INFO lines on stdout before the names.
            if (!n.isEmpty() && !n.contains(' ') && !n.startsWith("INFO") && !n.startsWith("WARNING")) names << n;
        }
        if (usedTool) *usedTool = true;
        return names;
    }
    if (usedTool) *usedTool = false;
    QDir d(avdHome());
    for (const auto &e : d.entryList({"*.ini"}, QDir::Files, QDir::Name)) names << e.left(e.size() - 4);
    return names;
}

bool AndroidEmulator::discover() {
    m_avds.clear();
    bool tool = false;
    const QStringList names = listAvdNames(&tool);
    for (const auto &name : names) {
        AndroidAvd a;
        a.name = name;
        a.api = "unknown";
        QMap<QString, QString> ini, cfg;
        QFile f(avdHome() + "/" + name + ".ini");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) ini = parseIni(QString::fromUtf8(f.readAll()));
        a.path = ini.value("path");
        if (a.path.isEmpty() && ini.contains("path.rel")) a.path = QFileInfo(avdHome()).absolutePath() + "/" + ini.value("path.rel");
        if (a.path.isEmpty()) a.path = avdHome() + "/" + name + ".avd";
        QFile c(QDir(a.path).filePath("config.ini"));
        if (c.open(QIODevice::ReadOnly | QIODevice::Text)) cfg = parseIni(QString::fromUtf8(c.readAll()));
        // config.ini is authoritative, the outer .ini only carries `target=android-NN`.
        QString abi = cfg.value("abi.type");
        if (abi.isEmpty()) abi = cfg.value("hw.cpu.arch");
        a.abi = abi.isEmpty() ? QString("unknown") : normaliseAbi(abi);
        QString api = apiFromSysdir(cfg.value("image.sysdir.1"));
        if (api.isEmpty()) api = apiFromSysdir(ini.value("target"));
        if (!api.isEmpty()) a.api = api;
        a.tag = cfg.value("tag.id");
        a.device = cfg.value("hw.device.name");
        a.displayName = cfg.value("avd.ini.displayname");
        m_avds.push_back(a);
    }
    return true;
}

bool AndroidEmulator::createAvd(const QString &name, const QString &packageName, const QString &device) {
    if (!QFileInfo::exists(m_avdManager)) return false;
    // `echo no` answers avdmanager's "custom hardware profile" prompt.
    QProcess p;
    p.start(m_avdManager, {"create", "avd", "-n", name, "-k", packageName, "-d", device, "--force"});
    if (!p.waitForStarted(3000)) return false;
    p.write("no\n");
    p.closeWriteChannel();
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished(500);
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

qint64 AndroidEmulator::startAvd(const QString &name, bool noWindow, const QString &logFile) {
    if (!QFileInfo::exists(m_emulator)) return -1;
    QStringList a = {"-avd", name, "-no-boot-anim", "-no-snapshot"};
    if (noWindow) a << "-no-window";
    QProcess p;
    p.setProgram(m_emulator);
    p.setArguments(a);
    if (!logFile.isEmpty()) {
        p.setStandardOutputFile(logFile, QIODevice::Append);
        p.setStandardErrorFile(logFile, QIODevice::Append);
    } else {
        p.setStandardOutputFile(QProcess::nullDevice());
        p.setStandardErrorFile(QProcess::nullDevice());
    }
    qint64 pid = -1;
    if (!p.startDetached(&pid)) return -1;
    return pid;
}

bool AndroidEmulator::start(const QString &name, bool noWindow) { return startAvd(name, noWindow) > 0; }

QString AndroidEmulator::serialFor(const QString &avdName) {
    QString out;
    if (!run(m_adb, {"devices"}, &out, 5000)) return {};
    for (const auto &l : out.split('\n', Qt::SkipEmptyParts)) {
        if (!l.startsWith("emulator-")) continue;
        const QString serial = l.section('\t', 0, 0).trimmed();
        QString nm;
        if (run(m_adb, {"-s", serial, "emu", "avd", "name"}, &nm, 5000) && nm.section('\n', 0, 0).trimmed() == avdName)
            return serial;
    }
    return {};
}

bool AndroidEmulator::stop(const QString &name) {
    const QString serial = serialFor(name);
    return !serial.isEmpty() && stopSerial(serial);
}

bool AndroidEmulator::stopSerial(const QString &serial) {
    QString out;
    return run(m_adb, {"-s", serial, "emu", "kill"}, &out, 10000);
}

bool AndroidEmulator::adb(const QStringList &args, QString *output) { return run(m_adb, args, output, 15000); }

QStringList AndroidEmulator::installedSystemImages() const {
    QStringList out;
    QDir root(m_sdkRoot + "/system-images");
    if (!root.exists()) return out;
    for (const auto &api : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir a(root.filePath(api));
        for (const auto &tag : a.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir b(a.filePath(tag));
            for (const auto &abi : b.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                out << "system-images;" + api + ";" + tag + ";" + abi;
        }
    }
    return out;
}

AndroidEmulatorInfo AndroidEmulator::info() const {
    return {m_sdkRoot, m_emulator, m_adb, m_avdManager, QFileInfo::exists(m_emulator) && QFileInfo::exists(m_adb)};
}

void AndroidEmulator::findSerialAsync(QObject *owner, const QString &avdName, std::function<void(const QString &)> cb) {
    const QString adb = m_adb;
    AsyncProcess::run(owner, adb, {"devices"}, 5000, [owner, adb, avdName, cb](const AsyncProcess::Result &r) {
        QStringList serials;
        if (r.ok())
            for (const auto &l : r.text().split('\n', Qt::SkipEmptyParts))
                if (l.startsWith("emulator-") && l.section('\t', 1, 1).trimmed() == "device") serials << l.section('\t', 0, 0).trimmed();
        auto step = QSharedPointer<std::function<void(int)>>::create();
        *step = [owner, adb, avdName, cb, serials, step](int i) {
            if (i >= serials.size()) {
                cb(QString());
                return;
            }
            AsyncProcess::run(owner, adb, {"-s", serials[i], "emu", "avd", "name"}, 5000,
                              [serials, i, avdName, cb, step](const AsyncProcess::Result &nr) {
                                  if (nr.ok() && nr.text().section('\n', 0, 0).trimmed() == avdName) {
                                      cb(serials[i]);
                                      return;
                                  }
                                  (*step)(i + 1);
                              });
        };
        (*step)(0);
    });
}
