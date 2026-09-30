#include "Issues.h"
#include "AndroidEmulator.h"
#include "AndroidPackageCatalog.h"
#include "AndroidRuntime.h"
#include "MatrixExecutor.h"
#include <QFileInfo>

QString remedyForFailure(const QString &step, const QString &message) {
    if (step == "boot") {
        if (message.contains("not found")) return "Install the emulator and platform-tools packages (sdkmanager \"emulator\" \"platform-tools\") and set ANDROID_HOME to that SDK.";
        if (message.contains("within")) return "Raise MOBILELAB_BOOT_TIMEOUT_S, or check that /dev/kvm is accessible; without KVM the emulator boots very slowly.";
        return "Start the AVD by hand with `emulator -avd <name>` and read its output.";
    }
    if (step == "abi") return "The running system image does not match the AVD configuration. Recreate the AVD with the intended x86_64 or arm64-v8a image.";
    if (step == "api") return "The system image API level differs from the AVD configuration. Recreate the AVD or refresh its config.ini.";
    if (step == "logcat") return "adb returned no log data. Check `adb -s <serial> logcat -d` and that the device is not offline.";
    if (step == "screenshot") return "The display may be off or the emulator runs headless without a GPU. Try `adb exec-out screencap -p`.";
    if (step == "shutdown") return "Stop the emulator manually with `adb -s <serial> emu kill`.";
    return QString();
}

QVector<Issue> collectIssues(const AppContext &ctx, int maxRuns) {
    QVector<Issue> out;
    auto add = [&](Issue::Severity sev, const QString &id, const QString &cat, const QString &group, const QString &title,
                   const QString &detail, const QString &remedy, Location where = {}) {
        Issue i;
        i.severity = sev; i.id = id; i.category = cat; i.group = group; i.title = title; i.detail = detail; i.remedy = remedy; i.where = where;
        out.push_back(i);
    };
    const QString env = "Environment";
    if (ctx.emulator) {
        const auto info = ctx.emulator->info();
        if (!QFileInfo::exists(info.emulatorPath))
            add(Issue::Error, "env.emulator", env, "Android SDK", "Android emulator not found",
                "Looked for " + info.emulatorPath, "Install it with sdkmanager \"emulator\" and point ANDROID_HOME at the SDK.");
        if (!QFileInfo::exists(info.adbPath))
            add(Issue::Error, "env.adb", env, "Android SDK", "adb not found", "Looked for " + info.adbPath,
                "Install platform-tools (sdkmanager \"platform-tools\") or put adb on PATH.");
        if (!QFileInfo::exists(info.cmdlineToolsPath))
            add(Issue::Warning, "env.avdmanager", env, "Android SDK", "avdmanager not found; new AVDs cannot be created",
                "Looked for " + info.cmdlineToolsPath, "Install cmdline-tools (sdkmanager \"cmdline-tools;latest\").");
        if (ctx.emulator->installedSystemImages().isEmpty())
            add(Issue::Warning, "env.images", env, "Android SDK", "No system images installed",
                "No directory under " + info.sdkRoot + "/system-images", "Install one, for example sdkmanager \"system-images;android-35;google_apis;x86_64\".");
    }
    if (ctx.runtime) {
        if (!ctx.runtime->kvmAvailable())
            add(Issue::Warning, "env.kvm", env, "Host", "KVM is unavailable",
                "/dev/kvm does not exist or is not accessible; x86_64 emulators run without acceleration.",
                "Enable virtualization in firmware and add your user to the kvm group: sudo usermod -aG kvm $USER.");
        if (ctx.runtime->targets().isEmpty())
            add(Issue::Info, "env.noavd", env, "Android SDK", "No AVDs found",
                "avdmanager and ~/.android/avd contain no virtual devices.", "Create one with File > New Virtual Device.");
    }
    if (!ctx.apiError.isEmpty())
        add(Issue::Error, "env.api", env, "REST API", "REST server could not start", ctx.apiError,
            "Port 4000 belongs to the Node backend. Set MOBILELAB_ANDROID_API_PORT to a free port (default 4100).");
    if (ctx.containerProbed && !ctx.containerViable)
        add(Issue::Info, "env.container", env, "Containers", "Waydroid container backend is not usable on this host",
            "ARM64 host, binder and Waydroid are required.", "Optional: install Waydroid on an ARM64 Linux host.");
    if (ctx.matrix) {
        const auto &recs = ctx.matrix->records();
        int shown = 0;
        for (int i = recs.size() - 1; i >= 0 && shown < maxRuns; --i, ++shown) {
            const auto &r = recs[i];
            for (const auto &t : r.targets) {
                for (const auto &s : t.steps) {
                    if (s.state != RunState::Failed) continue;
                    Location loc;
                    loc.kind = Location::RunTarget;
                    loc.id = r.id;
                    loc.sub = t.avd;
                    loc.tab = "logs";
                    loc.line = s.failLine;
                    add(Issue::Error, r.id + "/" + t.avd + "/" + s.name, "Failed Targets", r.id,
                        QString("%1: %2 failed").arg(t.avd, s.name), s.message, remedyForFailure(s.name, s.message), loc);
                }
            }
            if (r.state == RunState::Cancelled && !r.error.isEmpty()) {
                Location loc;
                loc.kind = Location::Run;
                loc.id = r.id;
                add(Issue::Warning, r.id + "/interrupted", "Failed Targets", r.id, "Run was interrupted", r.error, "Run the matrix again.", loc);
            }
        }
    }
    return out;
}
