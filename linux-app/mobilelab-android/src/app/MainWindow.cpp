#include "MainWindow.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPainter>
#include <QProcess>
#include <QRadialGradient>
#include <cmath>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include "ActivityLog.h"
#include "AndroidContainerRuntime.h"
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "ArtifactCollector.h"
#include "AvdWizard.h"
#include "EditorParts.h"
#include "GlassMenu.h"
#include "HostMetrics.h"
#include "Icons.h"
#include "Issues.h"
#include "MatrixExecutor.h"
#include "OpenQuickly.h"
#include "ResourceScheduler.h"
#include "UiUtil.h"
#include "glass/Glass.h"

// A short-lived glass pill above the editor status strip ("Screenshot saved", failures of actions).
class Toast : public GlassPanel {
public:
    explicit Toast(QWidget *parent) : GlassPanel(parent, Glass::Kind::Sheet) {
        setShape(Shape::Capsule);
        setShadowMargin(5);
        setObjectName("toast");
        m_timer.setParent(this);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { hide(); });
        setAccessibleName("Notification");
        hide();
    }
    void show(const QString &text, bool error) {
        m_text = text;
        m_error = error;
        setAccessibleDescription(text);
        const QFontMetrics fm(Theme::instance().ui(12, QFont::Medium));
        const int w = qMin(560, fm.horizontalAdvance(text) + 56 + 2 * shadowMargin());
        resize(w, 34 + 2 * shadowMargin());
        reposition();
        raise();
        QWidget::show();
        update();
        m_timer.start(error ? 5200 : 3200);
    }
    /// Centred, above the editor status strip and the log's "Jump to End" pill (which a toast at the very bottom
    /// used to cover) and clear of the toolbar and jump bar at the top.
    void reposition() {
        if (!parentWidget()) return;
        move((parentWidget()->width() - width()) / 2, parentWidget()->height() - height() - 104);
    }
    void paintContent(QPainter &p, const QRect &shape) override {
        Icons::paint(&p, m_error ? "exclamationmark.triangle" : "checkmark.diamond.fill", QRectF(shape.left() + 12, shape.center().y() - 8, 16, 16), m_error ? tk().warn : tk().pass);
        p.setFont(Theme::instance().ui(12, QFont::Medium));
        p.setPen(tk().text);
        p.drawText(shape.adjusted(36, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(m_text, Qt::ElideRight, shape.width() - 50));
    }
private:
    QString m_text;
    bool m_error = false;
    QTimer m_timer;
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("MobileLab Android");
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAccessibleName("MobileLab Android");
    // ---- core ----
    m_ctx.activity = new ActivityLog(this);
    m_ctx.host = new HostMetrics(this);
    m_ctx.emulator = new AndroidEmulator(this);
    m_ctx.artifacts = new ArtifactCollector();
    m_ctx.artifacts->setAdb(m_ctx.emulator->adbPath());
    m_ctx.runtime = new AndroidRuntime(this);
    m_ctx.runtime->setEmulator(m_ctx.emulator);
    m_ctx.scheduler = new ResourceScheduler(this);
    m_ctx.container = new AndroidContainerRuntime(this);
    m_ctx.matrix = new MatrixExecutor(m_ctx.emulator, m_ctx.scheduler, m_ctx.artifacts, this);
    m_ctx.api = new ApiServer(m_ctx.runtime, m_ctx.scheduler, this);
    connectCore();
    // Scheduler capacity from the machine: one slot pair per logical CPU, memory from /proc/meminfo.
    {
        m_ctx.host->sample();
        const int cpu = qMax(1, QThread::idealThreadCount());
        const int memMb = m_ctx.host->hostMemTotal() > 0 ? int(m_ctx.host->hostMemTotal() / (1024 * 1024)) : 2048;
        m_ctx.scheduler->configure(cpu, memMb);
    }
    if (!m_ctx.api->listen()) {
        m_ctx.apiError = QString("Cannot listen on 127.0.0.1:%1: %2").arg(ApiServer::configuredPort()).arg(m_ctx.api->lastError());
        m_ctx.activity->add("api", m_ctx.apiError);
    } else {
        m_ctx.activity->add("api", QString("REST listening on 127.0.0.1:%1").arg(m_ctx.api->port()));
    }
    m_ctx.runtime->probe();
    m_ctx.matrix->loadHistory();
    // The container probe may run `waydroid status` (slow): off the UI thread.
    {
        QThread *th = QThread::create([c = m_ctx.container] { c->probe(); });
        m_probeThread = th;
        connect(th, &QThread::finished, this, [this, th] {
            m_probeThread = nullptr;
            th->deleteLater();
            m_ctx.containerProbed = true;
            m_ctx.containerViable = m_ctx.container->diagnostics().value("arm64").toObject().value("container_backend_viable").toBool();
            m_navigator->refreshAll();
        });
        th->start();
    }
    loadSchemes();
    createActions();
    createUi();
    createMenus();
    restoreUiState();
    updateActions();
    updateToggleText();
    navigate(Location{}, true);   // the welcome page is the first history entry
    updateCapsule();
    m_tick.setParent(this);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &MainWindow::updateCapsule);
    m_save.setParent(this);
    m_save.setSingleShot(true);
    m_save.setInterval(600);
    connect(&m_save, &QTimer::timeout, this, &MainWindow::saveUiState);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); scheduleSave(); });
    connect(&Glass::Settings::instance(), &Glass::Settings::levelChanged, this, [this] { scheduleSave(); });
    connect(&Theme::instance(), &Theme::motionChanged, this, [this] { scheduleSave(); });
    m_ctx.activity->add("app", QString("MobileLab Android %1 started").arg(MOBILELAB_VERSION));
}

MainWindow::~MainWindow() {
    if (m_probeThread) m_probeThread->wait(5000);   // never destroy a running QThread
    delete m_ctx.artifacts;
}

void MainWindow::connectCore() {
    auto log = [this](const char *source) { return [this, source](const QString &m) { m_ctx.activity->add(source, m); }; };
    connect(m_ctx.runtime, &AndroidRuntime::logMessage, this, log("runtime"));
    connect(m_ctx.scheduler, &ResourceScheduler::logMessage, this, log("scheduler"));
    connect(m_ctx.matrix, &MatrixExecutor::logMessage, this, log("matrix"));
    connect(m_ctx.api, &ApiServer::logMessage, this, log("api"));
    connect(m_ctx.container, &AndroidContainerRuntime::logMessage, this, log("container"));
    connect(m_ctx.runtime, &AndroidRuntime::targetsChanged, this, [this] {
        if (!m_navigator) return;
        m_navigator->devices()->refresh();
        updateActions();
        updateCapsule();
    });
    connect(m_ctx.matrix, &MatrixExecutor::runStarted, this, [this] { m_tick.start(); updateActions(); });
    connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this](const QString &) {
        if (!m_navigator) return;
        m_navigator->refreshAll();
        updateCapsule();
        updateActions();
    });
    connect(m_ctx.matrix, &MatrixExecutor::runFinished, this, [this](const QString &id) {
        m_tick.stop();
        const auto *r = m_ctx.matrix->record(id);
        if (r) toast(r->state == RunState::Passed ? "Tests passed" : r->state == RunState::Cancelled ? "Run cancelled" : QString("Tests failed: %1 of %2 failed").arg(r->failedSteps()).arg(r->totalSteps()), r && r->state == RunState::Failed);
        updateCapsule();
        updateActions();
        m_navigator->refreshAll();
    });
    connect(m_ctx.scheduler, &ResourceScheduler::jobChanged, this, [this] { updateActions(); });
}

// ---- background -----------------------------------------------------------------------------------------

void MainWindow::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(this);
    // Soft 155deg three-stop tint (same stops as the web dashboard) plus two faint blobs.
    const qreal rad = 155.0 * M_PI / 180.0, dx = std::sin(rad), dy = -std::cos(rad);
    const qreal len = std::abs(width() * dx) + std::abs(height() * dy);
    const QPointF c(width() / 2.0, height() / 2.0), d(dx * len / 2, dy * len / 2);
    QLinearGradient g(c - d, c + d);
    g.setColorAt(0, t.windowTop);
    g.setColorAt(0.48, t.windowMid);
    g.setColorAt(1, t.windowBottom);
    p.fillRect(rect(), g);
    // a little coloured light behind the panels: something for the glass to refract
    QRadialGradient a(width() * 0.18, -height() * 0.05, width() * 0.6);
    a.setColorAt(0, t.blobA);
    a.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), a);
    QRadialGradient b(width() * 0.92, height() * 1.02, width() * 0.5);
    b.setColorAt(0, t.blobB);
    b.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), b);
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (m_toast) m_toast->reposition();
    scheduleSave();
}

// ---- actions --------------------------------------------------------------------------------------------

void MainWindow::createActions() {
    auto mk = [this](const QString &text, const QKeySequence &key, auto slot) {
        auto *a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };
    aNavigator = mk("Hide Navigator", QKeySequence("Ctrl+0"), [this] { showNavigator(!m_panes->isShown(m_navId)); });
    aNavigator->setCheckable(true);
    aInspector = mk("Hide Inspectors", QKeySequence("Ctrl+Alt+0"), [this] { showInspector(!m_panes->isShown(m_insId)); });
    aInspector->setCheckable(true);
    aDebug = mk("Show Debug Area", QKeySequence("Ctrl+Shift+Y"), [this] { showDebugArea(!m_editorPanes->isShown(m_dbgId)); });
    aDebug->setCheckable(true);
    aRun = mk("Run", QKeySequence("Ctrl+R"), [this] { runMatrix(); });
    aStop = mk("Stop", QKeySequence("Ctrl+."), [this] { stopRun(); });
    aRetry = mk("Run Failed Targets Again", QKeySequence("Ctrl+Shift+R"), [this] { handleAction("retry-failed", {}); });
    aBack = mk("Go Back", QKeySequence("Ctrl+Alt+Left"), [this] { goBack(); });
    aForward = mk("Go Forward", QKeySequence("Ctrl+Alt+Right"), [this] { goForward(); });
    aRelated = mk("Environment Settings", {}, [this] { openSettings(SettingsSheet::Environment); });
    aOptions = mk("Editor Options", {}, [this] {
        GlassMenu menu(this);
        connect(menu.addAction("Show Welcome"), &QAction::triggered, this, [this] { navigate(Location{}); });
        connect(menu.addAction("Open in VS Code"), &QAction::triggered, this, [this] { launchVsCode(); });
        connect(menu.addAction("Toggle Inspectors"), &QAction::triggered, this, [this] { showInspector(!m_panes->isShown(m_insId)); });
        menu.exec(QCursor::pos());
    });
    aAdd = mk("New Virtual Device", {}, [this] { newAvd(); });
    aClear = mk("Clear Console", QKeySequence("Ctrl+K"), [] {});
    aScreenshot = mk("Take Screenshot", {}, [this] { handleAction("screenshot", selectedTargetId()); });
    aNewAvd = mk("New Virtual Device...", QKeySequence("Ctrl+N"), [this] { newAvd(); });
    aQuickly = mk("Open Quickly...", QKeySequence("Ctrl+Shift+O"), [this] { openQuickly(); });
    aSettings = mk("Settings...", QKeySequence("Ctrl+,"), [this] { openSettings(); });
    aRefresh = mk("Refresh Targets", QKeySequence("F5"), [this] { refreshTargets(); });
    aStartDev = mk("Start Selected Device", {}, [this] { handleAction("start", selectedTargetId()); });
    aStopDev = mk("Stop Selected Device", {}, [this] { handleAction("stop", selectedTargetId()); });
    aRestartDev = mk("Restart Selected Device", {}, [this] { handleAction("restart", selectedTargetId()); });
    aProbe = mk("Run ABI Shell Probe", {}, [this] { handleAction("probe", selectedTargetId()); });
    aVsCode = mk("Open in VS Code", {}, [this] { launchVsCode(); });
    aFind = mk("Find in Workspace...", QKeySequence("Ctrl+Shift+F"), [this] { selectNavigator(Navigator::Find, true); });
    aQuit = mk("Quit MobileLab", QKeySequence("Ctrl+Q"), [this] { close(); });
    aShortcuts = mk("Keyboard Shortcuts...", QKeySequence("Ctrl+/"), [this] { showShortcuts(); });
    aDiagnostics = mk("Diagnostics...", {}, [this] { openSettings(SettingsSheet::Environment); });
    aReset = mk("Reset Layout", {}, [this] { resetLayout(); });
    aNextIssue = mk("Jump to Next Issue", QKeySequence("Ctrl+'"), [this] { nextIssue(1); });
    aPrevIssue = mk("Jump to Previous Issue", QKeySequence("Ctrl+Shift+'"), [this] { nextIssue(-1); });
    for (int i = 0; i < 6; ++i) {
        aNavTab[i] = mk(QString("%1 Navigator").arg(Navigator::tabName(i)), QKeySequence(QString("Ctrl+%1").arg(i + 1)), [this, i] { selectNavigator(i, true); });
    }
    for (int i = 0; i < 3; ++i) {
        aInsTab[i] = mk(QStringList{"Attributes Inspector", "History Inspector", "Quick Help Inspector"}[i], QKeySequence(QString("Ctrl+Alt+%1").arg(i + 1)), [this, i] {
            showInspector(true);
            m_inspector->setCurrentTab(i);
        });
    }
    aClear->setEnabled(true);
}

void MainWindow::createMenus() {
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    auto menu = [&](const QString &title) {
        auto *m = new GlassMenu(title, this);
        bar->addMenu(m);
        return m;
    };
    auto *app = menu("MobileLab");
    connect(app->addAction("About MobileLab"), &QAction::triggered, this, [this] {
        auto *s = new MessageSheet("About MobileLab Android",
                                   QString("Version %1\nAndroid device lab: AVDs, Waydroid and hybrid x86_64 + ARM64 matrix runs.\n\nQt %2. Interface fonts: Inter and JetBrains Mono (embedded, SIL OFL).").arg(MOBILELAB_VERSION, qVersion()), this);
        m_lastSheet = s;
        if (m_modalDialogs) { s->exec(); delete s; } else s->show();
    });
    app->addSeparator();
    app->addAction(aSettings);
    app->addSeparator();
    app->addAction(aQuit);

    auto *file = menu("File");
    file->addAction(aNewAvd);
    file->addAction(aQuickly);
    file->addSeparator();
    file->addAction(aRefresh);
    QAction *junit = file->addAction("Export JUnit for Current Run");
    connect(junit, &QAction::triggered, this, [this] { handleAction("export-junit", m_loc.id); });
    QAction *reveal = file->addAction("Open Artifacts Folder");
    connect(reveal, &QAction::triggered, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.artifacts->root())); });
    file->addSeparator();
    QAction *close = file->addAction("Close Window\tCtrl+W");
    connect(close, &QAction::triggered, this, [this] { this->close(); });

    auto *edit = menu("Edit");
    QAction *copy = edit->addAction("Copy\tCtrl+C");
    connect(copy, &QAction::triggered, this, [this] { sendKeyToFocus(QKeySequence::Copy); });
    QAction *all = edit->addAction("Select All\tCtrl+A");
    connect(all, &QAction::triggered, this, [this] { sendKeyToFocus(QKeySequence::SelectAll); });

    auto *view = menu("View");
    view->addAction(aNavigator);
    view->addAction(aInspector);
    view->addAction(aDebug);
    view->addSeparator();
    auto *navs = new GlassMenu("Navigators", this);
    for (auto *a : aNavTab) navs->addAction(a);
    view->addMenu(navs);
    auto *ins = new GlassMenu("Inspectors", this);
    for (auto *a : aInsTab) ins->addAction(a);
    view->addMenu(ins);
    view->addSeparator();
    auto *appearance = new GlassMenu("Appearance", this);
    auto *grp = new QActionGroup(this);
    const QStringList modes{"System", "Light", "Dark"};
    for (int i = 0; i < 3; ++i) {
        QAction *a = appearance->addAction(modes[i]);
        a->setCheckable(true);
        a->setChecked(int(Theme::instance().mode()) == i);
        grp->addAction(a);
        connect(a, &QAction::triggered, this, [i] { Theme::instance().setMode(Theme::Mode(i)); });
    }
    view->addMenu(appearance);
    auto *glassMenu = new GlassMenu("Liquid Glass", this);
    auto *ggrp = new QActionGroup(this);
    const QStringList levels{"Off", "Blur", "Full"};
    for (int i = 0; i < 3; ++i) {
        QAction *a = glassMenu->addAction(levels[i]);
        a->setCheckable(true);
        a->setChecked(int(Glass::level()) == i);
        ggrp->addAction(a);
        connect(a, &QAction::triggered, this, [i] { Glass::Settings::instance().setLevel(Glass::Level(i)); });
    }
    connect(&Glass::Settings::instance(), &Glass::Settings::levelChanged, glassMenu, [glassMenu, levels] {
        const auto acts = glassMenu->actions();
        for (int i = 0; i < acts.size(); ++i) acts[i]->setChecked(int(Glass::level()) == i);
    });
    view->addMenu(glassMenu);
    QAction *motion = view->addAction("Reduce Motion");
    motion->setCheckable(true);
    motion->setChecked(Theme::instance().reducedMotion());
    connect(motion, &QAction::toggled, this, [](bool on) { Theme::instance().setReducedMotion(on); });
    connect(&Theme::instance(), &Theme::motionChanged, motion, [motion] { motion->setChecked(Theme::instance().reducedMotion()); });
    view->addSeparator();
    view->addAction(aReset);

    auto *find = menu("Find");
    find->addAction(aFind);

    auto *nav = menu("Navigate");
    nav->addAction(aBack);
    nav->addAction(aForward);
    nav->addSeparator();
    nav->addAction(aNextIssue);
    nav->addAction(aPrevIssue);
    nav->addSeparator();
    QAction *rev = nav->addAction("Reveal in Navigator");
    connect(rev, &QAction::triggered, this, [this] { m_navigator->reveal(m_loc); });

    auto *ed = menu("Editor");
    connect(ed->addAction("Show Welcome"), &QAction::triggered, this, [this] { navigate(Location{}); });
    ed->addSeparator();
    for (const auto &t : {QString("summary"), QString("tests"), QString("logs")}) {
        QAction *a = ed->addAction(t == "summary" ? "Report Summary" : t == "tests" ? "Report Tests" : "Report Logs");
        connect(a, &QAction::triggered, this, [this, t] {
            Location l = m_loc;
            if (l.kind != Location::Run && l.kind != Location::RunTarget) {
                const auto &recs = m_ctx.matrix->records();
                if (recs.isEmpty()) return;
                l.kind = Location::Run;
                l.id = recs.last().id;
            }
            l.tab = t;
            l.line = -1;
            navigate(l);
        });
    }

    auto *product = menu("Product");
    product->addAction(aRun);
    product->addAction(aStop);
    product->addAction(aRetry);
    product->addSeparator();
    product->addAction(aStartDev);
    product->addAction(aStopDev);
    product->addAction(aRestartDev);
    product->addAction(aProbe);
    product->addAction(aScreenshot);
    product->addSeparator();
    auto *schemeMenu = new GlassMenu("Scheme", this);
    connect(schemeMenu, &QMenu::aboutToShow, this, [this, schemeMenu] {
        schemeMenu->clear();
        for (int i = 0; i < m_schemes.size(); ++i) {
            QAction *a = schemeMenu->addAction(m_schemes[i].title);
            a->setCheckable(true);
            a->setChecked(i == m_scheme);
            connect(a, &QAction::triggered, this, [this, i] { m_scheme = i; updateCapsule(); scheduleSave(); });
        }
    });
    product->addMenu(schemeMenu);
    auto *destMenu = new GlassMenu("Destination", this);
    connect(destMenu, &QMenu::aboutToShow, this, [this, destMenu] {
        destMenu->clear();
        QAction *all = destMenu->addAction("All Targets");
        all->setCheckable(true);
        all->setChecked(m_dest.isEmpty());
        connect(all, &QAction::triggered, this, [this] { setDestination({}); });
        destMenu->addSeparator();
        for (const auto &t : m_ctx.runtime->targets()) {
            QAction *a = destMenu->addAction(t.id);
            a->setCheckable(true);
            a->setChecked(m_dest.isEmpty() || m_dest.contains(t.id));
            const QString id = t.id;
            connect(a, &QAction::triggered, this, [this, id] {
                QStringList cur = m_dest;
                if (cur.isEmpty()) for (const auto &x : m_ctx.runtime->targets()) cur << x.id;
                if (cur.contains(id)) cur.removeAll(id); else cur << id;
                setDestination(cur);
            });
        }
    });
    product->addMenu(destMenu);

    auto *debug = menu("Debug");
    debug->addAction(aClear);
    connect(debug->addAction("Activate Console"), &QAction::triggered, this, [this] {
        showDebugArea(true);
        m_debug->console()->setFocus();
    });
    debug->addAction(aDebug);

    auto *integrate = menu("Integrate");
    integrate->addAction(aVsCode);
    QAction *waydroid = integrate->addAction("Show Waydroid Container");
    connect(waydroid, &QAction::triggered, this, [this] { Location l; l.kind = Location::Container; l.id = "waydroid"; navigate(l); });
    integrate->addAction(aDiagnostics);

    auto *window = menu("Window");
    QAction *minimize = window->addAction("Minimize\tCtrl+M");
    connect(minimize, &QAction::triggered, this, [this] { showMinimized(); });
    QAction *zoom = window->addAction("Zoom");
    connect(zoom, &QAction::triggered, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    window->addSeparator();
    window->addAction(aReset);

    auto *help = menu("Help");
    help->addAction(aShortcuts);
    help->addAction(aDiagnostics);
}

void MainWindow::sendKeyToFocus(const QKeySequence &seq) {
    QWidget *w = QApplication::focusWidget();
    if (!w) return;
    const QKeyCombination kc = seq.isEmpty() ? QKeyCombination() : seq[0];
    QKeyEvent press(QEvent::KeyPress, kc.key(), kc.keyboardModifiers());
    QApplication::sendEvent(w, &press);
}

// ---- UI -------------------------------------------------------------------------------------------------

void MainWindow::createUi() {
    auto *central = new QWidget(this);
    central->setAttribute(Qt::WA_NoSystemBackground, true);
    setCentralWidget(central);
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_toolbar = new Toolbar({aNavigator, aStop, aRun, aDebug, aInspector}, central);
    v->addWidget(m_toolbar);
    m_panes = new PaneHost(PaneHost::Orientation::Horizontal, central);
    m_panes->setFloating(true, 8, 8);
    v->addWidget(m_panes, 1);

    // navigator
    m_navPanel = new Panel(Panel::Role::Sidebar, m_panes);
    auto *nl = new QVBoxLayout(m_navPanel);
    nl->setContentsMargins(0, 4, 0, 0);
    m_navigator = new Navigator(m_ctx, m_navPanel);
    nl->addWidget(m_navigator);
    // editor card: editor area + debug area
    m_editorPanel = new Panel(Panel::Role::Editor, m_panes);
    auto *el = new QVBoxLayout(m_editorPanel);
    el->setContentsMargins(0, 0, 0, 0);
    m_editorPanes = new PaneHost(PaneHost::Orientation::Vertical, m_editorPanel);
    m_editorPanes->setFloating(false, 0, 1);
    el->addWidget(m_editorPanes);
    m_editor = new EditorArea(m_ctx, {aBack, aForward, aRelated, aOptions, aAdd}, m_editorPanes);
    m_debug = new DebugArea(m_ctx, {aDebug, aInspector, aRun, aStop, aRetry, aScreenshot, aClear}, m_editorPanes);
    m_editorPanes->setCenter(m_editor, 140);
    PaneHost::Spec dspec;
    dspec.minSize = 120;
    dspec.maxSize = 640;
    dspec.defaultSize = 240;
    dspec.collapsedSize = Metrics::debugBar;
    dspec.snap = 44;
    dspec.name = "Debug area";
    m_dbgId = m_editorPanes->addPane(PaneHost::Side::Trailing, m_debug, dspec);
    m_editorPanes->setShown(m_dbgId, false, false);
    // inspector
    m_inspectorPanel = new Panel(Panel::Role::Sidebar, m_panes);
    auto *il = new QVBoxLayout(m_inspectorPanel);
    il->setContentsMargins(0, 4, 0, 0);
    m_inspector = new Inspector(m_ctx, m_inspectorPanel);
    il->addWidget(m_inspector);

    PaneHost::Spec nspec;
    nspec.minSize = 220; nspec.maxSize = 460; nspec.defaultSize = 300; nspec.snap = 56; nspec.name = "Navigator";
    PaneHost::Spec ispec;
    ispec.minSize = 240; ispec.maxSize = 460; ispec.defaultSize = 300; ispec.snap = 56; ispec.name = "Inspector";
    m_panes->setCenter(m_editorPanel, 360);
    m_navId = m_panes->addPane(PaneHost::Side::Leading, m_navPanel, nspec);
    m_insId = m_panes->addPane(PaneHost::Side::Trailing, m_inspectorPanel, ispec);

    m_toast = new Toast(central);

    // ---- wiring ----
    connect(m_panes, &PaneHost::shownChanged, this, [this](int id, bool shown) {
        if (id == m_navId) { aNavigator->setChecked(shown); }
        if (id == m_insId) { aInspector->setChecked(shown); }
        updateToggleText();
        if (!shown) {
            QWidget *fw = QApplication::focusWidget();
            QWidget *pane = m_panes->pane(id);
            if (fw && pane->isAncestorOf(fw)) m_editor->currentEditor()->setFocus();
        }
        scheduleSave();
    });
    connect(m_panes, &PaneHost::sizeChanged, this, [this] { scheduleSave(); });
    connect(m_editorPanes, &PaneHost::shownChanged, this, [this](int, bool shown) {
        aDebug->setChecked(shown);
        m_debug->setExpanded(shown);
        updateToggleText();
        if (!shown) {
            QWidget *fw = QApplication::focusWidget();
            if (fw && m_debug->isAncestorOf(fw)) m_editor->currentEditor()->setFocus();
        }
        scheduleSave();
    });
    connect(m_editorPanes, &PaneHost::sizeChanged, this, [this] { scheduleSave(); });
    connect(m_navigator, &Navigator::locationRequested, this, [this](const Location &l) {
        if (l.kind == Location::Welcome && l.id.isEmpty()) return;   // group rows do not navigate
        navigate(l);
    });
    connect(m_navigator, &Navigator::previewRequested, this, [this](const Location &l) { navigate(l); });
    connect(m_navigator, &Navigator::action, this, &MainWindow::handleAction);
    connect(m_navigator, &Navigator::currentChanged, this, [this] { scheduleSave(); });
    connect(m_editor, &EditorArea::locationRequested, this, [this](const Location &l) { navigate(l); });
    connect(m_editor, &EditorArea::action, this, &MainWindow::handleAction);
    connect(m_inspector, &Inspector::locationRequested, this, [this](const Location &l) { navigate(l); });
    connect(m_toolbar->capsule(), &Capsule::schemeRequested, this, &MainWindow::showSchemeMenu);
    connect(m_toolbar->capsule(), &Capsule::destinationRequested, this, &MainWindow::showDestinationMenu);
    connect(m_toolbar->capsule(), &Capsule::statusRequested, this, &MainWindow::openReportFromStatus);
    connect(m_editor->jumpBar(), &JumpBar::gridRequested, this, [this](const QPoint &pos) {
        GlassMenu menu(this);
        if (m_history.isEmpty()) menu.addAction("No recent locations")->setEnabled(false);
        for (int i = m_history.size() - 1; i >= 0 && i > m_history.size() - 12; --i) {
            const Location l = m_history[i];
            const auto crumbs = m_editor->crumbsFor(l);
            QAction *a = menu.addAction(crumbs.isEmpty() ? "MobileLab" : crumbs.last().text);
            a->setCheckable(true);
            a->setChecked(i == m_hpos);
            connect(a, &QAction::triggered, this, [this, l] { navigate(l); });
        }
        menu.exec(pos);
    });
    connect(aClear, &QAction::triggered, this, [] {});
    if (m_ctx.api) {
        // keep the compiler honest about unused member warnings
    }
    setTabOrder(m_toolbar->rightGroup()->buttons().last(), m_navigator->tabBar()->tabButton(0));
}

// ---- panels ---------------------------------------------------------------------------------------------

void MainWindow::showNavigator(bool on, bool animate) {
    m_panes->setShown(m_navId, on, animate);
    if (on && animate) m_navigator->page(m_navigator->current())->focusTarget()->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::showInspector(bool on, bool animate) {
    m_panes->setShown(m_insId, on, animate);
}

void MainWindow::showDebugArea(bool on, bool animate) {
    m_editorPanes->setShown(m_dbgId, on, animate);
    if (on && animate) m_debug->console()->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::selectNavigator(int tab, bool focus) {
    if (!m_panes->isShown(m_navId)) showNavigator(true, true);
    m_navigator->setCurrent(tab, focus);
}

void MainWindow::updateToggleText() {
    aNavigator->setText(m_panes->isShown(m_navId) ? "Hide Navigator" : "Show Navigator");
    aInspector->setText(m_panes->isShown(m_insId) ? "Hide Inspectors" : "Show Inspectors");
    aDebug->setText(m_editorPanes->isShown(m_dbgId) ? "Hide Debug Area" : "Show Debug Area");
    aNavigator->setChecked(m_panes->isShown(m_navId));
    aInspector->setChecked(m_panes->isShown(m_insId));
    aDebug->setChecked(m_editorPanes->isShown(m_dbgId));
}

void MainWindow::resetLayout() {
    m_panes->setPaneSize(m_navId, 300);
    m_panes->setPaneSize(m_insId, 300);
    m_editorPanes->setPaneSize(m_dbgId, 240);
    showNavigator(true, true);
    showInspector(true, true);
    showDebugArea(false, true);
    scheduleSave();
}

// ---- navigation -----------------------------------------------------------------------------------------

void MainWindow::navigate(const Location &loc, bool addHistory) {
    if (loc.kind == Location::Settings) {
        openSettings(loc.id == "storage" ? SettingsSheet::Storage : loc.id == "general" ? SettingsSheet::General : SettingsSheet::Environment);
        return;
    }
    if (m_navigating) return;
    m_navigating = true;
    Location l = loc;
    if ((l.kind == Location::Run || l.kind == Location::RunTarget) && l.tab.isEmpty()) l.tab = "summary";
    m_loc = l;
    if (addHistory) {
        if (m_hpos >= 0 && m_history.value(m_hpos) == l) {
            // same place
        } else {
            if (m_hpos >= 0 && m_hpos < m_history.size() - 1) m_history.remove(m_hpos + 1, m_history.size() - m_hpos - 1);
            // Switching tabs of the same report replaces the entry instead of piling up history.
            if (m_hpos >= 0 && m_history[m_hpos].sameThing(l)) m_history[m_hpos] = l;
            else { m_history.push_back(l); m_hpos = m_history.size() - 1; }
        }
    }
    m_editor->showLocation(l);
    m_inspector->setLocation(l);
    m_debug->setLocation(l);
    m_navigator->reveal(l);
    updateHistoryActions();
    updateActions();
    m_navigating = false;
}

void MainWindow::updateHistoryActions() {
    aBack->setEnabled(m_hpos > 0);
    aForward->setEnabled(m_hpos >= 0 && m_hpos < m_history.size() - 1);
}

void MainWindow::goBack() {
    if (m_hpos <= 0) return;
    --m_hpos;
    navigate(m_history[m_hpos], false);
}

void MainWindow::goForward() {
    if (m_hpos < 0 || m_hpos >= m_history.size() - 1) return;
    ++m_hpos;
    navigate(m_history[m_hpos], false);
}

QString MainWindow::selectedTargetId() const {
    if (m_loc.kind == Location::Target) return m_loc.id;
    if (m_loc.kind == Location::RunTarget) return m_loc.sub;
    return {};
}

void MainWindow::nextIssue(int direction) {
    const auto issues = collectIssues(m_ctx);
    QVector<Issue> nav;
    for (const auto &i : issues)
        if (i.category == "Failed Targets") nav << i;
    if (nav.isEmpty()) { toast("No issues to jump to"); return; }
    m_issueCursor = (m_issueCursor + direction + nav.size()) % nav.size();
    selectNavigator(Navigator::Issues, false);
    navigate(nav[m_issueCursor].where);
}

void MainWindow::openReportFromStatus() {
    const auto &recs = m_ctx.matrix->records();
    if (recs.isEmpty()) { selectNavigator(Navigator::Devices, false); return; }
    Location l;
    l.kind = Location::Run;
    l.id = m_ctx.matrix->isRunning() ? m_ctx.matrix->currentRunId() : recs.last().id;
    l.tab = "summary";
    navigate(l);
}

// ---- capsule --------------------------------------------------------------------------------------------

void MainWindow::loadSchemes() {
    QStringList dirs;
    const QString env = qEnvironmentVariable("MOBILELAB_ANDROID_CONFIG");
    if (!env.isEmpty()) dirs << env;
    dirs << QCoreApplication::applicationDirPath() + "/../share/mobilelab-android/config";
#ifdef MOBILELAB_CONFIG_DIR
    dirs << QString(MOBILELAB_CONFIG_DIR);
#endif
    for (const auto &d : dirs) {
        const auto files = QDir(d + "/matrix").entryInfoList({"*.yaml", "*.yml"}, QDir::Files, QDir::Name);
        if (files.isEmpty()) continue;
        for (const auto &fi : files) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            Scheme s;
            s.file = fi.absoluteFilePath();
            s.title = fi.completeBaseName();
            enum { None, Abis, Arch } sect = None;
            for (const auto &raw : QString::fromUtf8(f.readAll()).split('\n')) {
                const QString line = raw.trimmed();
                if (line.startsWith("name:") && !raw.startsWith(' ')) s.title = line.mid(5).trimmed().remove('"');
                if (raw == "abis:") { sect = Abis; continue; }
                if (raw == "architectures:") { sect = Arch; continue; }
                if (!raw.startsWith(' ') && !raw.startsWith('-') && !raw.isEmpty()) { sect = None; continue; }
                if (sect == Abis && line.startsWith("- name:")) s.abis << line.mid(7).trimmed().remove('"');
                if (sect == Arch && line.startsWith("- ")) s.abis << line.mid(2).trimmed();
            }
            m_schemes << s;
        }
        break;
    }
    if (m_schemes.isEmpty()) m_schemes << Scheme{QString(), "Android Matrix (all targets)", {}};
    // the hybrid matrix is the default scheme
    for (int i = 0; i < m_schemes.size(); ++i)
        if (m_schemes[i].file.contains("hybrid")) m_scheme = i;
}

QString MainWindow::destinationText() const {
    const auto targets = m_ctx.runtime->targets();
    if (targets.isEmpty()) return "No Devices";
    if (m_dest.isEmpty()) return targets.size() == 1 ? targets.first().id : QString("All Targets (%1)").arg(targets.size());
    if (m_dest.size() == 1) return m_dest.first();
    return QString("%1 Targets").arg(m_dest.size());
}

void MainWindow::setDestination(const QStringList &names) {
    QStringList all;
    for (const auto &t : m_ctx.runtime->targets()) all << t.id;
    m_dest = names;
    if (names.size() == all.size() && !all.isEmpty()) {
        bool same = true;
        for (const auto &a : all) if (!names.contains(a)) same = false;
        if (same) m_dest.clear();
    }
    updateCapsule();
    scheduleSave();
}

void MainWindow::updateCapsule() {
    if (!m_toolbar) return;
    Capsule::State s;
    s.scheme = "Android Matrix";
    s.destination = destinationText();
    const auto &recs = m_ctx.matrix->records();
    auto timeText = [](const QDateTime &d) {
        const QDateTime l = d.toLocalTime();
        return (l.date() == QDate::currentDate() ? QString("Today at ") : l.toString("d MMM yyyy") + " at ") + QLocale().toString(l.time(), "h:mm AP");
    };
    if (m_ctx.matrix->isRunning()) {
        const auto *r = m_ctx.matrix->record(m_ctx.matrix->currentRunId());
        int done = 0, started = 0;
        if (r)
            for (const auto &t : r->targets) {
                if (t.state != RunState::Pending) ++started;
                if (t.state == RunState::Passed || t.state == RunState::Failed || t.state == RunState::Cancelled) ++done;
            }
        if (started == 0) { s.state = "Queued"; s.detail = "waiting for a free slot"; }
        else {
            const qint64 secs = r ? r->durationMs() / 1000 : 0;
            s.state = "Running";
            s.spinning = true;
            s.detail = QString("%1 of %2 targets, %3:%4").arg(done).arg(r ? r->targets.size() : 0).arg(secs / 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'));
        }
    } else if (!recs.isEmpty()) {
        const auto &r = recs.last();
        if (r.state == RunState::Passed) { s.state = "Tests Passed"; s.detail = timeText(r.finished.isValid() ? r.finished : r.started); }
        else if (r.state == RunState::Failed) { s.state = "Tests Failed"; s.tone = Capsule::Tone::Fail; s.detail = QString("%1 of %2 tests failed").arg(r.failedSteps()).arg(r.totalSteps()); }
        else { s.state = "Cancelled"; s.detail = timeText(r.finished.isValid() ? r.finished : r.started); }
    } else if (!m_ctx.emulator->info().available) {
        s.state = "Ready";
        s.tone = Capsule::Tone::Warn;
        s.detail = "Android SDK not found";
    } else if (m_ctx.runtime->targets().isEmpty()) {
        s.state = "Ready";
        s.detail = "No virtual devices";
    } else {
        s.state = "Ready";
        s.detail = QString("%1 %2, %3 running").arg(m_ctx.runtime->targets().size()).arg(m_ctx.runtime->targets().size() == 1 ? "target" : "targets").arg(m_ctx.runtime->runningCount());
    }
    m_toolbar->capsule()->setState(s);
    if (m_debug) m_debug->setStatus(s.state, s.detail);
}

void MainWindow::showSchemeMenu(const QPoint &below) {
    GlassMenu menu(this);
    for (int i = 0; i < m_schemes.size(); ++i) {
        QAction *a = menu.addAction(m_schemes[i].title);
        a->setCheckable(true);
        a->setChecked(i == m_scheme);
        a->setToolTip(m_schemes[i].abis.join(", "));
        connect(a, &QAction::triggered, this, [this, i] { m_scheme = i; updateCapsule(); scheduleSave(); toast("Scheme: " + m_schemes[i].title + (m_schemes[i].abis.isEmpty() ? QString() : "  (" + m_schemes[i].abis.join(", ") + ")")); });
    }
    menu.addSeparator();
    QAction *edit = menu.addAction("Edit Scheme...");
    edit->setEnabled(!m_schemes.value(m_scheme).file.isEmpty());
    connect(edit, &QAction::triggered, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_schemes.value(m_scheme).file)); });
    menu.exec(below);
}

void MainWindow::showDestinationMenu(const QPoint &below) {
    for (;;) {
        GlassMenu menu(this);
        QAction *all = menu.addAction("All Targets");
        all->setCheckable(true);
        all->setChecked(m_dest.isEmpty());
        menu.addSeparator();
        QHash<QAction *, QString> ids;
        for (const auto &t : m_ctx.runtime->targets()) {
            QAction *a = menu.addAction(QString("%1    (API %2, %3)").arg(t.id, t.api, t.arch));
            a->setCheckable(true);
            a->setChecked(m_dest.isEmpty() || m_dest.contains(t.id));
            ids[a] = t.id;
        }
        menu.addSeparator();
        QAction *manage = menu.addAction("Manage Devices...");
        QAction *fresh = menu.addAction("New Virtual Device...");
        QAction *chosen = menu.exec(below);
        if (!chosen) return;
        if (chosen == all) { setDestination({}); continue; }
        if (chosen == manage) { selectNavigator(Navigator::Devices, false); return; }
        if (chosen == fresh) { newAvd(); return; }
        QStringList cur = m_dest;
        if (cur.isEmpty()) for (const auto &t : m_ctx.runtime->targets()) cur << t.id;
        const QString id = ids.value(chosen);
        if (cur.contains(id)) { if (cur.size() > 1) cur.removeAll(id); } else cur << id;
        setDestination(cur);   // the menu is shown again so several targets can be ticked in a row
    }
}

// ---- running --------------------------------------------------------------------------------------------

QVector<MatrixTarget> MainWindow::targetsFor(const QStringList &only) const {
    QVector<MatrixTarget> out;
    const QStringList abis = m_schemes.value(m_scheme).abis;
    for (const auto &t : m_ctx.runtime->targets()) {
        if (!only.isEmpty()) { if (!only.contains(t.id)) continue; }
        else {
            if (!m_dest.isEmpty() && !m_dest.contains(t.id)) continue;
            if (!abis.isEmpty() && !abis.contains(t.arch)) continue;
        }
        out.push_back({t.id, t.api, t.arch, "android-test"});
    }
    return out;
}

void MainWindow::runMatrix(const QStringList &only) {
    if (m_ctx.matrix->isRunning()) { toast("A run is already in progress"); return; }
    const auto targets = targetsFor(only);
    if (targets.isEmpty()) {
        const QString why = m_ctx.runtime->targets().isEmpty() ? "Create or install an Android Virtual Device first (File > New Virtual Device)."
                                                                : "No target matches the selected scheme and destination.";
        auto *s = new MessageSheet("Nothing to run", why, this, true);
        m_lastSheet = s;
        if (m_modalDialogs) { s->exec(); delete s; } else s->show();
        return;
    }
    m_ctx.activity->add("matrix", QString("executing %1 Android target(s)").arg(targets.size()));
    const QString id = m_ctx.matrix->run(targets);
    if (id.isEmpty()) return;
    Location l;
    l.kind = Location::Run;
    l.id = id;
    l.tab = "summary";
    navigate(l);
    updateCapsule();
    updateActions();
}

void MainWindow::stopRun() {
    if (m_ctx.matrix->isRunning()) {
        m_ctx.matrix->cancel();
        toast("Cancelling run");
        return;
    }
    const QString id = selectedTargetId();
    const auto *t = id.isEmpty() ? nullptr : m_ctx.runtime->target(id);
    if (t && (t->state == "running" || t->state == "booting")) handleAction("stop", id);
}

void MainWindow::refreshTargets() {
    m_ctx.runtime->probe();
    if (!m_ctx.matrix->isRunning()) m_ctx.matrix->loadHistory();
    m_navigator->refreshAll();
    m_editor->refresh();
    updateCapsule();
    toast(QString("%1 targets found").arg(m_ctx.runtime->targets().size()));
}

void MainWindow::updateActions() {
    if (!m_navigator) return;
    const bool running = m_ctx.matrix->isRunning();
    const QString id = selectedTargetId();
    const AndroidTarget *t = id.isEmpty() ? nullptr : m_ctx.runtime->target(id);
    aRun->setEnabled(!running && !m_ctx.runtime->targets().isEmpty());
    aStop->setEnabled(running || (t && (t->state == "running" || t->state == "booting")));
    aStartDev->setEnabled(t && t->state == "stopped");
    aStopDev->setEnabled(t && (t->state == "running" || t->state == "booting"));
    aRestartDev->setEnabled(t && t->state == "running");
    aProbe->setEnabled(t && t->state == "running");
    aScreenshot->setEnabled(t && t->state == "running");
    bool anyFailed = false;
    const MatrixRunRecord *r = (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) ? m_ctx.matrix->record(m_loc.id) : (m_ctx.matrix->records().isEmpty() ? nullptr : &m_ctx.matrix->records().last());
    if (r) for (const auto &tg : r->targets) if (tg.state == RunState::Failed) anyFailed = true;
    aRetry->setEnabled(!running && anyFailed);
    aRun->setToolTip(QString("Run  (%1)").arg(Ui::shortcutText(aRun->shortcut())));
}

void MainWindow::handleAction(const QString &name, const QString &id) {
    if (name == "new-avd") newAvd();
    else if (name == "run") runMatrix();
    else if (name == "stop-run") stopRun();
    else if (name == "run-here") runMatrix({id});
    else if (name == "run-again") {
        const auto *r = m_ctx.matrix->record(id);
        if (!r) return;
        QStringList ids;
        for (const auto &t : r->targets) ids << t.avd;
        runMatrix(ids);
    } else if (name == "retry-failed") {
        const MatrixRunRecord *r = (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) ? m_ctx.matrix->record(m_loc.id) : (m_ctx.matrix->records().isEmpty() ? nullptr : &m_ctx.matrix->records().last());
        if (!r) return;
        QStringList ids;
        for (const auto &t : r->targets) if (t.state == RunState::Failed) ids << t.avd;
        if (ids.isEmpty()) { toast("No failed targets"); return; }
        runMatrix(ids);
    } else if (name == "start") {
        if (!m_ctx.runtime->start(id)) toast("Could not start " + id + ": see the console", true);
    } else if (name == "stop") {
        if (id.isEmpty()) stopRun();
        else if (!m_ctx.runtime->stop(id)) toast("Could not stop " + id, true);
    } else if (name == "restart") {
        m_ctx.runtime->restart(id);
    } else if (name == "probe") {
        if (!m_ctx.runtime->shell(id, "getprop ro.product.cpu.abi")) toast("The device must be running to probe it", true);
        else showDebugArea(true);
    } else if (name == "screenshot") {
        if (id.isEmpty()) { toast("Select a running device first", true); return; }
        const QString dir = m_ctx.artifacts->root() + "/screenshots";
        QDir().mkpath(dir);
        const QString path = QString("%1/%2-%3.png").arg(dir, id, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
        m_ctx.runtime->screenshotAsync(id, path, [this, path, id](bool ok, const QString &why) {
            if (ok) { m_ctx.activity->add("avd", "screenshot saved: " + path); toast("Screenshot saved for " + id); }
            else { m_ctx.activity->add("avd", "screenshot failed: " + why); toast("Screenshot failed: " + why, true); }
        });
    } else if (name == "copy-name") {
        QApplication::clipboard()->setText(id);
        toast("Copied " + id);
    } else if (name == "export-junit") {
        const auto *r = m_ctx.matrix->record(id);
        if (!r) { toast("Select a run first", true); return; }
        QString path;
        if (RunEditor::exportJUnit(*r, &path)) { m_ctx.activity->add("matrix", "JUnit written: " + path); toast("JUnit exported: " + path); m_editor->refresh(); }
        else toast("Could not write junit.xml", true);
    } else if (name == "diagnostics") openSettings(SettingsSheet::Environment);
    else if (name == "settings") openSettings();
    else if (name == "shortcuts") showShortcuts();
    else if (name == "vscode") launchVsCode();
    else if (name == "refresh") refreshTargets();
    else if (name == "host-info") { navigate(Location{}); showInspector(true); m_inspector->setCurrentTab(Inspector::Attributes); }
}

// ---- dialogs --------------------------------------------------------------------------------------------

void MainWindow::newAvd() {
    auto *w = new AvdWizard(m_ctx, this);
    m_lastSheet = w;
    connect(w, &AvdWizard::created, this, [this](const QString &name) {
        m_ctx.activity->add("avd", "created " + name);
        m_ctx.runtime->refreshTargets();
        m_navigator->refreshAll();
        Location l;
        l.kind = Location::Target;
        l.id = name;
        navigate(l);
        toast("Created " + name);
    });
    if (m_modalDialogs) { w->exec(); delete w; }
    else w->show();
}

void MainWindow::openSettings(SettingsSheet::Pane pane) {
    auto *s = new SettingsSheet(m_ctx, this);
    m_lastSheet = s;
    s->showPane(pane);
    connect(s, &SettingsSheet::rerunChecks, this, [this] { m_ctx.runtime->probe(); m_navigator->refreshAll(); updateCapsule(); });
    connect(s, &SettingsSheet::artifactsCleaned, this, [this] { m_navigator->refreshAll(); m_editor->refresh(); });
    if (m_modalDialogs) { s->exec(); delete s; saveUiState(); }
    else s->show();
}

void MainWindow::openQuickly() {
    auto *q = new OpenQuickly(m_ctx, this);
    m_lastSheet = q;
    connect(q, &OpenQuickly::locationChosen, this, [this](const Location &l) { navigate(l); });
    connect(q, &OpenQuickly::commandChosen, this, [this](const QString &c) { handleAction(c, {}); });
    if (m_modalDialogs) { q->exec(); delete q; }
    else q->show();
}

void MainWindow::showShortcuts() {
    auto *s = new ShortcutsSheet(this);
    m_lastSheet = s;
    if (m_modalDialogs) { s->exec(); delete s; }
    else s->show();
}

void MainWindow::launchVsCode() {
    const QString bin = QStandardPaths::findExecutable("code");
    if (bin.isEmpty()) {
        m_ctx.activity->add("ide", "VS Code CLI not found. Install code or add it to PATH.");
        auto *s = new MessageSheet("VS Code unavailable", "The VS Code command line tool (code) was not found on PATH.\nInstall VS Code or run \"Shell Command: Install 'code' command in PATH\" from its command palette.", this, true);
        m_lastSheet = s;
        if (m_modalDialogs) { s->exec(); delete s; } else s->show();
        return;
    }
    QProcess::startDetached(bin, {QDir::currentPath()});
    m_ctx.activity->add("ide", "launched VS Code for parallel app development");
    toast("Opened VS Code");
}

void MainWindow::toast(const QString &text, bool error) {
    if (m_toast) m_toast->show(text, error);
    m_ctx.activity->add("app", text);
}

// ---- state ----------------------------------------------------------------------------------------------

void MainWindow::scheduleSave() {
    if (m_restored) m_save.start();
}

void MainWindow::saveUiState() {
    if (!m_restored) return;
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("layout/navigatorWidth", m_panes->paneSize(m_navId));
    s.setValue("layout/navigatorShown", m_panes->isShown(m_navId));
    s.setValue("layout/inspectorWidth", m_panes->paneSize(m_insId));
    s.setValue("layout/inspectorShown", m_panes->isShown(m_insId));
    s.setValue("layout/debugHeight", m_editorPanes->paneSize(m_dbgId));
    s.setValue("layout/debugShown", m_editorPanes->isShown(m_dbgId));
    s.setValue("navigator/tab", m_navigator->current());
    s.setValue("inspector/tab", m_inspector->currentTab());
    s.setValue("destination/selected", m_dest);
    s.setValue("scheme/index", m_scheme);
    Theme::instance().save(s);
    Glass::Settings::instance().save(s);
    s.sync();
}

void MainWindow::restoreUiState() {
    QSettings s;
    if (s.contains("window/geometry")) restoreGeometry(s.value("window/geometry").toByteArray());
    else resize(1477, 959);
    Glass::Settings::instance().load(s);
    m_panes->setPaneSize(m_navId, s.value("layout/navigatorWidth", 300).toInt());
    m_panes->setPaneSize(m_insId, s.value("layout/inspectorWidth", 300).toInt());
    m_editorPanes->setPaneSize(m_dbgId, s.value("layout/debugHeight", 240).toInt());
    m_panes->setShown(m_navId, s.value("layout/navigatorShown", true).toBool(), false);
    m_panes->setShown(m_insId, s.value("layout/inspectorShown", true).toBool(), false);
    const bool dbg = s.value("layout/debugShown", false).toBool();
    m_editorPanes->setShown(m_dbgId, dbg, false);
    m_debug->setExpanded(dbg);
    m_navigator->setCurrent(qBound(0, s.value("navigator/tab", 0).toInt(), 5), false);
    m_inspector->setCurrentTab(qBound(0, s.value("inspector/tab", 0).toInt(), 2));
    m_dest = s.value("destination/selected").toStringList();
    QStringList valid;
    for (const auto &d : m_dest) if (m_ctx.runtime->target(d)) valid << d;
    m_dest = valid;
    m_scheme = qBound(0, s.value("scheme/index", m_scheme).toInt(), qMax(0, int(m_schemes.size()) - 1));
    m_restored = true;
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (m_ctx.matrix->isRunning()) m_ctx.matrix->cancel();
    saveUiState();
    QMainWindow::closeEvent(e);
}
