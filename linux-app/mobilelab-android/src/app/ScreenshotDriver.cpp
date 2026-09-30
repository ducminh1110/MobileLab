#include "ScreenshotDriver.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QMenuBar>
#include <QPainter>
#include <QTextStream>
#include <QThread>
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "HostMetrics.h"
#include "ArtifactCollector.h"
#include "GlassMenu.h"
#include "MainWindow.h"
#include "MatrixExecutor.h"
#include "OpenQuickly.h"
#include "glass/Glass.h"

namespace {
QString g_dir;
QStringList g_manifest;
int g_failures = 0;

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(4);
    } while (t.elapsed() < ms);
}

template <class F>
bool waitUntil(F pred, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(8);
    }
    return true;
}

void save(const QString &name, const QImage &img) {
    const QString path = g_dir + "/" + name + ".png";
    if (img.isNull() || !img.save(path)) {
        ++g_failures;
        g_manifest << "FAILED " + name;
        return;
    }
    g_manifest << QString("ok %1 %2x%3").arg(name).arg(img.width()).arg(img.height());
}

QImage grabWindow(MainWindow *w) {
    pump(120);
    QImage img = w->grab().toImage();
    return img;
}

// Window grab with a popup window (menu, sheet) composited at its screen position.
QImage grabWithPopup(MainWindow *w, QWidget *popup) {
    pump(220);
    QImage base = w->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage pop(popup->size() * popup->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    pop.setDevicePixelRatio(popup->devicePixelRatioF());
    pop.fill(Qt::transparent);
    popup->render(&pop);
    QPainter p(&base);
    p.drawImage(w->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), pop);
    return base;
}
}  // namespace

ScreenshotDriver::ScreenshotDriver(MainWindow *w) : QObject(w), m_w(w) {}

bool ScreenshotDriver::requested() { return !qEnvironmentVariable("MOBILELAB_SCREENSHOT_DIR").isEmpty(); }

void ScreenshotDriver::start() { QTimer::singleShot(400, this, &ScreenshotDriver::step); }

void ScreenshotDriver::finish(int code) {
    QFile f(g_dir + "/manifest.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        for (const auto &l : g_manifest) ts << l << "\n";
    }
    QCoreApplication::exit(code);
}

void ScreenshotDriver::step() {
    MainWindow *w = m_w;
    g_dir = qEnvironmentVariable("MOBILELAB_SCREENSHOT_DIR");
    QDir().mkpath(g_dir);
    w->setModalDialogsEnabled(false);
    const QString sizeEnv = qEnvironmentVariable("MOBILELAB_SCREENSHOT_SIZE", "1477x959");
    const QStringList sz = sizeEnv.split('x');
    w->resize(sz.value(0).toInt() > 0 ? sz[0].toInt() : 1477, sz.value(1).toInt() > 0 ? sz[1].toInt() : 959);
    w->activateWindow();
    w->raise();
    pump(500);
    auto &ctx = w->context();
    g_manifest << QString("size %1x%2").arg(w->width()).arg(w->height());
    g_manifest << QString("theme %1").arg(Theme::instance().dark() ? "dark" : "light");
    g_manifest << QString("glass %1").arg(Glass::levelName(Glass::level()));
    g_manifest << QString("ui-font %1 (asked Inter) mono-font %2 (asked JetBrains Mono)").arg(QFontInfo(Theme::instance().ui(13)).family(), QFontInfo(Theme::instance().mono(12)).family());
    g_manifest << QString("targets %1").arg(ctx.runtime->targets().size());
    g_manifest << QString("api-port %1").arg(ctx.api ? int(ctx.api->port()) : 0);
    Glass::resetStats();

    // 1: welcome
    w->selectNavigator(Navigator::Devices, false);
    w->navigate(Location{}, false);
    pump(300);
    save("01-welcome", grabWindow(w));

    // 2: a device selected
    QString first;
    for (const auto &t : ctx.runtime->targets())
        if (first.isEmpty() || t.arch == "x86_64") { if (first.isEmpty() || t.arch == "x86_64") first = t.id; }
    Location tl;
    tl.kind = Location::Target;
    tl.id = first;
    if (!first.isEmpty()) {
        w->navigate(tl);
        w->navigator()->page(Navigator::Devices)->tree()->setFocus();
        save("02-devices-target-stopped", grabWindow(w));
    }

    // 3: run the matrix on the fixture and capture it while it runs
    w->runMatrix();
    waitUntil([&] { return ctx.matrix->isRunning(); }, 3000);
    // mid-run: at least one target is booting or testing
    waitUntil([&] {
        const auto *r = ctx.matrix->record(ctx.matrix->currentRunId());
        if (!r) return false;
        for (const auto &t : r->targets) if (t.state == RunState::Running) return true;
        return false;
    }, 6000);
    pump(1100);
    w->selectNavigator(Navigator::Tests, false);
    save("03-run-in-progress", grabWindow(w));
    const bool finished = waitUntil([&] { return !ctx.matrix->isRunning(); }, 120000);
    if (!finished) { g_manifest << "FAILED matrix run did not finish"; ++g_failures; }
    pump(500);
    const auto &recs = ctx.matrix->records();
    QString runId;
    if (!recs.isEmpty()) runId = recs.last().id;

    // 4: report tabs
    if (!runId.isEmpty()) {
        Location rl;
        rl.kind = Location::Run;
        rl.id = runId;
        rl.tab = "summary";
        w->navigate(rl);
        save("04-report-summary", grabWindow(w));
        rl.tab = "tests";
        w->navigate(rl);
        save("05-report-tests", grabWindow(w));
        // logs, scrolled to the first failure when there is one
        Location ll;
        ll.kind = Location::RunTarget;
        ll.id = runId;
        ll.tab = "logs";
        for (const auto &t : recs.last().targets)
            for (const auto &s : t.steps)
                if (s.state == RunState::Failed && ll.sub.isEmpty()) { ll.sub = t.avd; ll.line = s.failLine; }
        if (ll.sub.isEmpty()) { ll.sub = recs.last().targets.first().avd; ll.line = 8; }
        w->navigate(ll);
        w->showDebugArea(false, false);
        save("06-report-logs-failure", grabWindow(w));
        // a run level log with the execution line at the end
        Location rl2;
        rl2.kind = Location::Run;
        rl2.id = runId;
        rl2.tab = "logs";
        w->navigate(rl2);
        save("07-run-log", grabWindow(w));
    }

    // 5: navigators
    w->selectNavigator(Navigator::Tests, false);
    if (!runId.isEmpty()) {
        Location l;
        l.kind = Location::RunTarget;
        l.id = runId;
        l.sub = recs.last().targets.first().avd;
        l.tab = "tests";
        w->navigate(l);
        w->navigator()->page(Navigator::Tests)->tree()->setFocus();
    }
    save("08-tests-navigator", grabWindow(w));
    w->selectNavigator(Navigator::Issues, false);
    {
        auto *tree = w->navigator()->page(Navigator::Issues)->tree();
        tree->setFocus();
        // open the first failed step
        const auto issues = w->navigator()->issues();
        Q_UNUSED(issues);
        Location il;
        for (const auto &r : recs)
            for (const auto &t : r.targets)
                for (const auto &s : t.steps)
                    if (s.state == RunState::Failed && il.id.isEmpty()) { il.kind = Location::RunTarget; il.id = r.id; il.sub = t.avd; il.tab = "logs"; il.line = s.failLine; }
        if (!il.id.isEmpty()) w->navigate(il);
    }
    save("09-issues-navigator", grabWindow(w));
    w->selectNavigator(Navigator::Find, false);
    w->navigator()->find()->setQuery("abi");
    pump(200);
    save("10-find-navigator", grabWindow(w));
    w->navigator()->find()->setQuery({});
    w->selectNavigator(Navigator::Reports, false);
    save("11-reports-navigator", grabWindow(w));
    w->selectNavigator(Navigator::Devices, false);

    // 6: debug navigator + debug area with variables and console (reference 2)
    w->selectNavigator(Navigator::Debug, false);
    if (!first.isEmpty()) w->navigate(tl);
    w->showDebugArea(true, false);
    pump(400);
    w->context().host->sample();
    pump(2100);
    save("12-debug-navigator-and-area", grabWindow(w));
    if (!runId.isEmpty()) {
        Location ll;
        ll.kind = Location::RunTarget;
        ll.id = runId;
        ll.tab = "logs";
        for (const auto &t : recs.last().targets)
            for (const auto &s : t.steps)
                if (s.state == RunState::Failed && ll.sub.isEmpty()) { ll.sub = t.avd; ll.line = s.failLine; }
        if (ll.sub.isEmpty()) { ll.sub = recs.last().targets.first().avd; ll.line = 6; }
        w->navigate(ll);
        save("13-debug-area-run-console", grabWindow(w));
    }
    w->showDebugArea(false, false);

    // 7: a running device with a real screenshot (fake adb screencap)
    if (!first.isEmpty()) {
        ctx.runtime->start(first);
        const bool up = waitUntil([&] { const auto *t = ctx.runtime->target(first); return t && t->state == "running"; }, 25000);
        g_manifest << QString("device-running %1").arg(up ? "yes" : "no");
        w->navigate(tl);
        waitUntil([&] { return w->editor()->target()->hasImage(); }, 8000);
        w->selectNavigator(Navigator::Devices, false);
        w->navigator()->page(Navigator::Devices)->tree()->setFocus();
        save("14-target-preview-running", grabWindow(w));
        g_manifest << QString("preview-image %1").arg(w->editor()->target()->hasImage() ? "yes" : ("no: " + w->editor()->target()->placeholderReason()));
    }

    // 8: collapsed panels
    w->showNavigator(false, false);
    w->showInspector(false, false);
    save("15-panels-collapsed", grabWindow(w));
    w->showNavigator(true, false);
    w->showInspector(true, false);
    w->showDebugArea(true, false);
    w->showDebugArea(false, false);

    // 9: popups and sheets
    {
        w->navigate(Location{}, false);
        w->openQuickly();
        auto *q = qobject_cast<OpenQuickly *>(w->lastSheet());
        if (q) {
            q->setQuery("pix");
            pump(200);
            save("16-open-quickly", grabWithPopup(w, q));
            delete q;
        }
        w->openSettings(SettingsSheet::General);
        if (auto *s = w->lastSheet()) { save("17-settings-general", grabWithPopup(w, s)); static_cast<SettingsSheet *>(s)->showPane(SettingsSheet::Environment); pump(200); save("18-settings-environment", grabWithPopup(w, s)); static_cast<SettingsSheet *>(s)->showPane(SettingsSheet::Storage); pump(300); save("19-settings-storage", grabWithPopup(w, s)); delete s; }
        w->newAvd();
        if (auto *s = w->lastSheet()) { save("20-avd-wizard", grabWithPopup(w, s)); delete s; }
        w->showShortcuts();
        if (auto *s = w->lastSheet()) { save("21-shortcuts", grabWithPopup(w, s)); delete s; }
        // product menu
        for (QAction *a : w->menuBar()->actions()) {
            if (a->text() != "Product") continue;
            QMenu *m = a->menu();
            m->popup(w->mapToGlobal(QPoint(420, 26)));
            pump(250);
            save("22-menu-product", grabWithPopup(w, m));
            m->hide();
        }
        // destination popup
        {
            GlassMenu menu(w);
            menu.addAction("All Targets")->setCheckable(true);
            menu.actions().first()->setChecked(true);
            menu.addSeparator();
            for (const auto &t : ctx.runtime->targets()) {
                QAction *a = menu.addAction(QString("%1    (API %2, %3)").arg(t.id, t.api, t.arch));
                a->setCheckable(true);
                a->setChecked(true);
            }
            menu.addSeparator();
            menu.addAction("Manage Devices...");
            menu.addAction("New Virtual Device...");
            const QRect cr = w->toolbar()->capsule()->regionRect(Capsule::Destination);
            menu.popup(w->toolbar()->capsule()->mapToGlobal(QPoint(cr.left(), cr.bottom() + 4)));
            pump(250);
            save("23-capsule-destination-menu", grabWithPopup(w, &menu));
            menu.hide();
        }
    }

    // 10: glass measurements: a resize storm forces every glass widget to re-sample its backdrop
    {
        Glass::resetStats();
        const QSize base = w->size();
        for (int i = 0; i < 24; ++i) {
            w->resize(base.width() - (i % 2) * 6, base.height());
            pump(30);
        }
        w->resize(base);
        pump(300);
        QFile f(g_dir + "/glass-stats.txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "glass level " << Glass::levelName(Glass::level()) << ", dpr " << w->devicePixelRatioF() << "\n";
            ts << "widget                 paints recomputes  paint avg ms  material avg ms  material max ms\n";
            const auto stats = Glass::allStats();
            QStringList keys = stats.keys();
            keys.sort();
            for (const auto &k : keys) {
                const auto &s = stats[k];
                ts << QString("%1 %2 %3 %4 %5 %6\n").arg(k, -22).arg(s.paints, 6).arg(s.recomputes, 9)
                          .arg(s.paints ? s.totalMs / s.paints : 0.0, 12, 'f', 3).arg(s.recomputes ? s.materialTotalMs / s.recomputes : 0.0, 15, 'f', 3).arg(s.materialMaxMs, 15, 'f', 3);
            }
        }
    }
    save("24-final", grabWindow(w));
    finish(g_failures ? 2 : 0);
}
