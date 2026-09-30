#include "AndroidRuntime.h"
#include "AndroidEmulator.h"
#include "AsyncProcess.h"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QSysInfo>

AndroidRuntime::AndroidRuntime(QObject *parent) : QObject(parent) {
    m_pollTimer.setParent(this);
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &AndroidRuntime::refreshStates);
}

void AndroidRuntime::setEmulator(AndroidEmulator *emulator) { m_androidEmulator = emulator; }

QString AndroidRuntime::findExecutable(const QStringList &names) const {
    for (const auto &n : names) {
        const auto p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) return p;
    }
    return {};
}

QString AndroidRuntime::classifyTargetStability(const QString &abi) const {
    if (abi.contains("x86_64", Qt::CaseInsensitive)) return "preferred";
    if (abi.contains("arm64", Qt::CaseInsensitive) || abi.contains("aarch64", Qt::CaseInsensitive)) return "fundamental";
    return "limited";
}

QStringList AndroidRuntime::deriveTags(const AndroidTarget &target) const {
    QStringList tags;
    tags << target.backend << target.stability;
    if (target.arch.contains("x86_64", Qt::CaseInsensitive)) tags << "abi:x86_64" << "focus";
    if (target.arch.contains("arm64", Qt::CaseInsensitive) || target.arch.contains("aarch64", Qt::CaseInsensitive))
        tags << "abi:arm64" << "compat";
    if (target.state == "running") tags << "running";
    if (target.api != "unknown") tags << "api:" + target.api;
    tags.removeDuplicates();
    return tags;
}

// Heuristic compatibility score (0..100): base 70, +10 running, +15 preferred ABI, -10 fundamental ABI,
// -20 when the emulator would run without KVM, -5 when the API level could not be read.
int AndroidRuntime::calculateHealthScore(const AndroidTarget &target) const {
    int score = 70;
    if (target.state == "running") score += 10;
    if (target.stability == "preferred") score += 15;
    if (target.stability == "fundamental") score -= 10;
    if (!m_kvm && target.backend.contains("emulator")) score -= 20;
    if (target.api == "unknown") score -= 5;
    return qBound(0, score, 100);
}

bool AndroidRuntime::probe() {
    m_arch = QSysInfo::currentCpuArchitecture();
    m_kernel = QSysInfo::kernelVersion();
    m_kvm = QFile::exists("/dev/kvm");
    m_arm64Host = m_arch.contains("arm", Qt::CaseInsensitive) || m_arch.contains("aarch64", Qt::CaseInsensitive);
    m_x86_64Host = m_arch.contains("x86_64", Qt::CaseInsensitive) || m_arch.contains("amd64", Qt::CaseInsensitive);
    m_qemu = !findExecutable({"qemu-system-x86_64", "qemu-system-aarch64", "qemu-system-arm"}).isEmpty();
    m_emulator = m_androidEmulator ? m_androidEmulator->info().available : !findExecutable({"emulator"}).isEmpty();
    m_supportedAbis.clear();
    m_hasArm64Abi = false;
    m_hasX8664Abi = false;
    if (m_androidEmulator) {
        for (const auto &image : m_androidEmulator->installedSystemImages()) {
            const auto abi = image.section(';', -1);
            if (!m_supportedAbis.contains(abi)) m_supportedAbis << abi;
            if (abi.contains("arm64", Qt::CaseInsensitive)) m_hasArm64Abi = true;
            if (abi.contains("x86_64", Qt::CaseInsensitive)) m_hasX8664Abi = true;
        }
    }
    emit logMessage(QString("Hybrid Android runtime probe: host=%1 kernel=%2 kvm=%3 qemu=%4 emulator=%5 abis=%6")
                        .arg(m_arch, m_kernel, m_kvm ? "yes" : "no", m_qemu ? "yes" : "no", m_emulator ? "yes" : "no",
                             m_supportedAbis.join(",")));
    if (m_hasArm64Abi)
        emit logMessage("ARM64 ABI is enabled but remains fundamental: package availability and runtime compatibility are stricter than x86_64.");
    if (m_hasX8664Abi)
        emit logMessage("x86_64 ABI is enabled as the preferred near-term Android emulator path while compatibility gaps are burned down.");
    if (!m_kvm) emit logMessage("KVM unavailable; accelerated emulator workloads are downgraded or excluded by the scheduler.");
    refreshTargets();
    return true;
}

void AndroidRuntime::refreshTargets() {
    QHash<QString, AndroidTarget> previous;
    for (const auto &t : m_targets) previous.insert(t.id, t);
    m_targets.clear();
    if (m_androidEmulator) {
        m_androidEmulator->discover();
        for (const auto &a : m_androidEmulator->avds()) {
            AndroidTarget t;
            t.id = a.name;
            t.api = a.api;
            t.arch = a.abi;
            t.tag = a.tag;
            t.device = a.device;
            t.backend = "google-emulator";
            t.stability = classifyTargetStability(a.abi);
            if (previous.contains(a.name)) {
                const auto &old = previous[a.name];
                t.state = old.state;
                t.serial = old.serial;
                t.pid = old.pid;
            }
            t.tags = deriveTags(t);
            t.healthScore = calculateHealthScore(t);
            m_targets.push_back(t);
        }
    }
    emit targetsChanged();
    refreshStates();
}

int AndroidRuntime::indexOf(const QString &id) const {
    for (int i = 0; i < m_targets.size(); ++i)
        if (m_targets[i].id == id) return i;
    return -1;
}

const AndroidTarget *AndroidRuntime::target(const QString &id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_targets[i];
}

int AndroidRuntime::runningCount() const {
    int n = 0;
    for (const auto &t : m_targets)
        if (t.state == "running") ++n;
    return n;
}

void AndroidRuntime::schedulePoll() {
    bool transitional = false;
    for (const auto &t : m_targets)
        if (t.state == "booting" || t.state == "stopping") transitional = true;
    m_pollTimer.start(transitional ? 1000 : 5000);
}

// Asks adb which emulators are attached, maps them to AVD names with `emu avd name`, and for
// targets that are booting checks sys.boot_completed. Runs entirely asynchronously.
void AndroidRuntime::refreshStates() {
    if (!m_androidEmulator || !m_emulator || m_probing) {
        schedulePoll();
        return;
    }
    m_probing = true;
    const QString adb = m_androidEmulator->adbPath();
    AsyncProcess::run(this, adb, {"devices"}, 5000, [this, adb](const AsyncProcess::Result &r) {
        QStringList serials;
        if (r.ok())
            for (const auto &l : r.text().split('\n', Qt::SkipEmptyParts))
                if (l.startsWith("emulator-") && l.section('\t', 1, 1).trimmed() == "device") serials << l.section('\t', 0, 0).trimmed();
        auto map = QSharedPointer<QHash<QString, QString>>::create();
        auto booted = QSharedPointer<QStringList>::create();
        auto step = QSharedPointer<std::function<void(int)>>::create();
        *step = [this, adb, serials, map, booted, step](int i) {
            if (i >= serials.size()) {
                m_probing = false;
                finishStateProbe(*map, *booted);
                return;
            }
            const QString serial = serials[i];
            AsyncProcess::run(this, adb, {"-s", serial, "emu", "avd", "name"}, 5000,
                              [this, adb, serial, map, booted, step, i](const AsyncProcess::Result &nr) {
                                  const QString name = nr.text().section('\n', 0, 0).trimmed();
                                  if (nr.ok() && !name.isEmpty()) map->insert(name, serial);
                                  AsyncProcess::run(this, adb, {"-s", serial, "shell", "getprop", "sys.boot_completed"}, 5000,
                                                    [booted, serial, step, i](const AsyncProcess::Result &br) {
                                                        if (br.ok() && br.text().trimmed() == "1") booted->append(serial);
                                                        (*step)(i + 1);
                                                    });
                              });
        };
        (*step)(0);
    });
}

void AndroidRuntime::finishStateProbe(const QHash<QString, QString> &map, const QStringList &bootedSerials) {
    bool changed = false;
    for (auto &t : m_targets) {
        const QString serial = map.value(t.id);
        QString state;
        if (!serial.isEmpty()) state = bootedSerials.contains(serial) ? "running" : "booting";
        else if (t.state == "booting" && t.pid > 0) state = "booting";  // process launched, adb not attached yet
        else state = "stopped";
        if (t.state == "stopping" && !serial.isEmpty()) state = "stopping";
        if (state == "booting" && serial.isEmpty()) {
            // Give a freshly launched process a bounded grace period; then give up honestly.
            if (!QFile::exists(QString("/proc/%1").arg(t.pid))) state = "stopped";
        }
        if (state != t.state || serial != t.serial) {
            t.state = state;
            t.serial = serial;
            if (state == "stopped") t.pid = -1;
            t.tags = deriveTags(t);
            t.healthScore = calculateHealthScore(t);
            changed = true;
            emit targetChanged(t.id);
        }
    }
    if (changed) emit targetsChanged();
    schedulePoll();
}

bool AndroidRuntime::start(const QString &id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    if (m_targets[i].state == "running" || m_targets[i].state == "booting") return true;
    if (!m_androidEmulator || !m_emulator) {
        emit logMessage("Cannot start " + id + ": Android emulator binary not found under " +
                        (m_androidEmulator ? m_androidEmulator->sdkRoot() : QString("the Android SDK")));
        return false;
    }
    const qint64 pid = m_androidEmulator->startAvd(id, true);
    if (pid <= 0) {
        emit logMessage("Failed to launch the emulator for " + id);
        return false;
    }
    m_targets[i].pid = pid;
    m_targets[i].state = "booting";
    m_targets[i].tags = deriveTags(m_targets[i]);
    emit logMessage("Started Android target " + id + " [" + m_targets[i].arch + ", " + m_targets[i].stability + "] pid " + QString::number(pid));
    emit targetChanged(id);
    emit targetsChanged();
    m_pollTimer.start(1000);
    return true;
}

bool AndroidRuntime::stop(const QString &id) {
    const int i = indexOf(id);
    if (i < 0 || !m_androidEmulator) return false;
    if (m_targets[i].serial.isEmpty()) {
        emit logMessage("Target " + id + " is not attached to adb; nothing to stop");
        return false;
    }
    m_targets[i].state = "stopping";
    emit targetChanged(id);
    emit targetsChanged();
    AsyncProcess::run(this, m_androidEmulator->adbPath(), {"-s", m_targets[i].serial, "emu", "kill"}, 15000,
                      [this, id](const AsyncProcess::Result &r) {
                          emit logMessage(r.ok() ? "Stopped target " + id : "Stop failed for " + id + ": " + r.errText().trimmed());
                          m_pollTimer.start(500);
                      });
    return true;
}

bool AndroidRuntime::restart(const QString &id) {
    const bool ok = stop(id);
    if (!ok) return false;
    // start() is issued once the state probe sees the emulator gone.
    auto conn = QSharedPointer<QMetaObject::Connection>::create();
    *conn = connect(this, &AndroidRuntime::targetChanged, this, [this, id, conn](const QString &changed) {
        if (changed != id) return;
        const auto *t = target(id);
        if (t && t->state == "stopped") {
            QObject::disconnect(*conn);
            start(id);
        }
    });
    return true;
}

bool AndroidRuntime::shell(const QString &id, const QString &command) {
    const int i = indexOf(id);
    if (i < 0 || m_targets[i].state != "running" || m_targets[i].serial.isEmpty() || !m_androidEmulator) return false;
    AsyncProcess::run(this, m_androidEmulator->adbPath(), {"-s", m_targets[i].serial, "shell", command}, 15000,
                      [this, id, command](const AsyncProcess::Result &r) {
                          emit logMessage("[" + id + "] $ " + command);
                          const QString out = (r.text() + r.errText()).trimmed();
                          if (!out.isEmpty()) emit logMessage("[" + id + "] " + out);
                          if (!r.ok()) emit logMessage("[" + id + "] command failed (exit " + QString::number(r.exitCode) + ")");
                      });
    return true;
}

bool AndroidRuntime::screenshot(const QString &id, const QString &path) {
    const int i = indexOf(id);
    if (i < 0 || m_targets[i].state != "running" || m_targets[i].serial.isEmpty() || !m_androidEmulator) return false;
    QProcess p;
    p.start(m_androidEmulator->adbPath(), {"-s", m_targets[i].serial, "exec-out", "screencap", "-p"});
    if (!p.waitForFinished(15000) || p.exitCode() != 0) return false;
    const QByteArray png = p.readAllStandardOutput();
    if (!png.startsWith("\x89PNG")) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(png);
    return f.commit();
}

void AndroidRuntime::screenshotAsync(const QString &id, const QString &path, std::function<void(bool, const QString &)> done) {
    const int i = indexOf(id);
    if (i < 0 || !m_androidEmulator) {
        done(false, "unknown target");
        return;
    }
    if (m_targets[i].state != "running" || m_targets[i].serial.isEmpty()) {
        done(false, "target is not running");
        return;
    }
    AsyncProcess::run(this, m_androidEmulator->adbPath(), {"-s", m_targets[i].serial, "exec-out", "screencap", "-p"}, 15000,
                      [path, done](const AsyncProcess::Result &r) {
                          if (!r.ok()) {
                              done(false, r.timedOut ? "adb screencap timed out" : ("adb screencap failed: " + r.errText().trimmed()));
                              return;
                          }
                          if (!r.out.startsWith("\x89PNG")) {
                              done(false, "adb screencap returned no PNG data");
                              return;
                          }
                          QSaveFile f(path);
                          if (!f.open(QIODevice::WriteOnly) || f.write(r.out) != r.out.size() || !f.commit()) {
                              done(false, "cannot write " + path);
                              return;
                          }
                          done(true, {});
                      });
}

QJsonObject AndroidRuntime::status() const {
    return {{"architecture", m_arch}, {"kernel", m_kernel}, {"kvm", m_kvm}, {"qemu", m_qemu},
            {"android_emulator", m_emulator}, {"arm64_host", m_arm64Host}, {"x86_64_host", m_x86_64Host},
            {"arm64_abi", m_hasArm64Abi}, {"x86_64_abi", m_hasX8664Abi}, {"hybrid_abi", hybridAbiAvailable()},
            {"supported_abis", m_supportedAbis.join(",")}, {"targets", m_targets.size()}};
}
