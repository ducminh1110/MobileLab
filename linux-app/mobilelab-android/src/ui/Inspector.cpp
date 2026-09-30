#include "Inspector.h"
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QTimer>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QSysInfo>
#include <QVBoxLayout>
#include "ActivityLog.h"
#include "AndroidContainerRuntime.h"
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "ArtifactCollector.h"
#include "EditorParts.h"
#include "Icons.h"
#include "MatrixExecutor.h"
#include "ResourceScheduler.h"
#include "UiUtil.h"

namespace {

// One key/value row: right aligned label in an 84 px column, value left aligned.
class KeyValueRow : public QWidget {
public:
    KeyValueRow(const QString &key, const QString &value, bool mono, const QColor &dot, bool copy, QWidget *parent) : QWidget(parent), m_key(key), m_value(value), m_mono(mono), m_dot(dot), m_copy(copy) {
        setMinimumHeight(Metrics::navRow);
        setToolTip(value);
        setAccessibleName(key);
        setAccessibleDescription(value);
        setMouseTracking(true);
    }
    QSize sizeHint() const override { return QSize(240, heightFor(width() > 0 ? width() : 240)); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return heightFor(w); }
protected:
    void paintEvent(QPaintEvent *) override {
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(Theme::instance().ui(11.5));
        p.setPen(t.textSecondary);
        p.drawText(QRect(0, 0, 84, Metrics::navRow), Qt::AlignRight | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(m_key, Qt::ElideRight, 84));
        int x = 94;
        if (m_dot.isValid()) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dot);
            p.drawEllipse(QPointF(x + 4, Metrics::navRow / 2.0), 4, 4);
            x += 14;
        }
        const int right = width() - 10 - (m_copy ? 22 : 0);
        p.setFont(m_mono ? Theme::instance().mono(11) : Theme::instance().ui(12));
        p.setPen(t.text);
        p.drawText(QRect(x, 3, right - x, height() - 3), Qt::AlignLeft | Qt::AlignTop | Qt::TextWrapAnywhere, m_value);
        if (m_copy) {
            Icons::paint(&p, "doc.on.doc", QRectF(width() - 24, 4, 14, 14), m_copyHover ? t.accent : t.textTertiary);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const bool h = m_copy && e->position().x() > width() - 28 && e->position().y() < 24;
        if (h != m_copyHover) { m_copyHover = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (m_copy && e->position().x() > width() - 28 && e->position().y() < 24) QApplication::clipboard()->setText(m_value);
    }
    void leaveEvent(QEvent *) override { m_copyHover = false; update(); }
private:
    int heightFor(int w) const {
        const QFont f = m_mono ? Theme::instance().mono(11) : Theme::instance().ui(12);
        const int avail = qMax(40, w - 94 - 10 - (m_dot.isValid() ? 14 : 0) - (m_copy ? 22 : 0));
        const QRect br = QFontMetrics(f).boundingRect(QRect(0, 0, avail, 4000), Qt::TextWrapAnywhere | Qt::AlignLeft, m_value);
        return qMax(Metrics::navRow, br.height() + 6);
    }
    QString m_key, m_value;
    bool m_mono, m_copy, m_copyHover = false;
    QColor m_dot;
};

class SectionHeader : public QAbstractButton {
public:
    SectionHeader(const QString &title, QWidget *parent) : QAbstractButton(parent), m_title(title) {
        setCheckable(true);
        setChecked(true);
        setFixedHeight(26);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(title);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Icons::paint(&p, isChecked() ? "chevron.down" : "chevron.right", QRectF(10, height() / 2.0 - 4, 8, 8), t.textTertiary);
        p.setFont(Theme::instance().ui(11, QFont::Bold));
        p.setPen(t.text);
        p.drawText(QRect(24, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        if (hasFocus()) Ui::drawFocusRing(&p, QRectF(rect()).adjusted(4, 2, -4, -2), 5);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) click();
        else QAbstractButton::keyPressEvent(e);
    }
private:
    QString m_title;
};

struct Row { QString key, value; bool mono = false; QColor dot; bool copy = false; };

QWidget *makeSection(const QString &title, const QVector<Row> &rows, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 4);
    v->setSpacing(0);
    auto *head = new SectionHeader(title, w);
    v->addWidget(head);
    auto *body = new QWidget(w);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(0);
    for (const auto &r : rows) bv->addWidget(new KeyValueRow(r.key, r.value, r.mono, r.dot, r.copy, body));
    v->addWidget(body);
    QObject::connect(head, &QAbstractButton::toggled, body, &QWidget::setVisible);
    return w;
}

QString fmtTime(const QDateTime &d) { return d.isValid() ? d.toLocalTime().toString("ddd d MMM yyyy, HH:mm:ss") : "Not yet"; }
QString fmtMs(qint64 ms) { return ms <= 0 ? "0 s" : QString::number(ms / 1000.0, 'f', ms < 10000 ? 2 : 1) + " s"; }

QScrollArea *makeScroll(QWidget *parent) {
    auto *s = new QScrollArea(parent);
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->viewport()->setAutoFillBackground(false);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return s;
}

QWidget *paragraphPage(const QVector<QPair<QString, QString>> &blocks, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(4);
    for (const auto &b : blocks) {
        if (!b.first.isEmpty()) {
            auto *h = new ThemedLabel(b.first, 11, QFont::Bold, ThemedLabel::Role::Text, w);
            h->setContentsMargins(0, 8, 0, 0);
            v->addWidget(h);
        }
        auto *l = new ThemedLabel(b.second, 12, QFont::Normal, ThemedLabel::Role::Secondary, w);
        l->setWordWrap(true);
        v->addWidget(l);
    }
    v->addStretch();
    return w;
}
}  // namespace

Inspector::Inspector(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_tabs = new NavTabBar(this);
    m_tabs->setObjectName("inspector-tabs");
    m_tabs->setTabs({{"doc.text", "Attributes Inspector", QKeySequence("Ctrl+Alt+1")},
                     {"clock", "History Inspector", QKeySequence("Ctrl+Alt+2")},
                     {"questionmark.circle", "Quick Help Inspector", QKeySequence("Ctrl+Alt+3")}});
    auto *wrap = new QWidget(this);
    auto *wh = new QHBoxLayout(wrap);
    wh->setContentsMargins(52, 2, 52, 0);
    wh->addWidget(m_tabs);
    wrap->setFixedHeight(Metrics::navTabBar);
    v->addWidget(wrap);
    m_stack = new QStackedWidget(this);
    m_attr = makeScroll(this);
    m_hist = makeScroll(this);
    m_help = makeScroll(this);
    m_attr->setAccessibleName("Attributes");
    m_hist->setAccessibleName("History");
    m_help->setAccessibleName("Quick Help");
    m_stack->addWidget(m_attr);
    m_stack->addWidget(m_hist);
    m_stack->addWidget(m_help);
    v->addWidget(m_stack, 1);
    connect(m_tabs, &NavTabBar::currentChanged, this, [this](int i) { m_stack->setCurrentIndex(i); refresh(); });
    m_coalesce = new QTimer(this);
    m_coalesce->setSingleShot(true);
    m_coalesce->setInterval(150);
    connect(m_coalesce, &QTimer::timeout, this, [this] {
        switch (m_stack->currentIndex()) {
        case Attributes: rebuildAttributes(); break;
        case History: rebuildHistory(); break;
        default: rebuildHelp(); break;
        }
    });
    if (m_ctx.runtime) connect(m_ctx.runtime, &AndroidRuntime::targetsChanged, this, [this] { m_coalesce->start(); });
    if (m_ctx.matrix) connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this] { m_coalesce->start(); });
    if (m_ctx.activity) connect(m_ctx.activity, &ActivityLog::added, this, [this] { if (m_stack->currentIndex() == History) m_coalesce->start(); });
    connect(&Theme::instance(), &Theme::changed, this, [this] { refresh(); });
    setAccessibleName("Inspector");
    rebuildAttributes();
}

void Inspector::setCurrentTab(int i) {
    m_tabs->setCurrent(i, false);
    m_stack->setCurrentIndex(i);
    refresh();
}

void Inspector::setLocation(const Location &loc) {
    m_loc = loc;
    refresh();
}

void Inspector::refresh() {
    switch (m_stack->currentIndex()) {
    case Attributes: rebuildAttributes(); break;
    case History: rebuildHistory(); break;
    default: rebuildHelp(); break;
    }
}

void Inspector::rebuildAttributes() {
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 4, 0, 12);
    v->setSpacing(0);
    const Tokens &t = tk();
    const QString stabilityNote = "preferred = x86_64, fundamental = ARM64, limited = others";
    if (m_loc.kind == Location::Target && m_ctx.runtime && m_ctx.runtime->target(m_loc.id)) {
        const AndroidTarget &g = *m_ctx.runtime->target(m_loc.id);
        v->addWidget(makeSection("Identity and Type", {{"Name", g.id}, {"Android API", g.api}, {"ABI", g.arch}, {"System Image", g.tag.isEmpty() ? "unknown" : g.tag},
                                                       {"Hardware", g.device.isEmpty() ? "unknown" : g.device}, {"Backend", g.backend}, {"Stability", g.stability}}, content));
        QString path;
        if (m_ctx.emulator)
            for (const auto &a : m_ctx.emulator->avds())
                if (a.name == g.id) path = a.path;
        v->addWidget(makeSection("Location", {{"AVD Path", path, true, {}, true}, {"AVD Home", m_ctx.emulator ? m_ctx.emulator->avdHome() : QString(), true, {}, true}}, content));
        v->addWidget(makeSection("Status", {{"State", g.state, false, Ui::statusColor(g.state, t)}, {"adb Serial", g.serial.isEmpty() ? "not attached" : g.serial, true, {}, !g.serial.isEmpty()},
                                            {"Process", g.pid > 0 ? QString::number(g.pid) : "not started by this app", true}, {"Score", QString("%1 / 100 (%2)").arg(g.healthScore).arg("heuristic")}}, content));
        QVector<Row> res;
        res << Row{"Cost", QString("%1 units of %2").arg(m_ctx.matrix ? m_ctx.matrix->costForAbi(g.arch) : 1).arg(m_ctx.scheduler ? m_ctx.scheduler->capacity() : 0)};
        if (m_ctx.scheduler)
            for (const auto &j : m_ctx.scheduler->runningJobs())
                if (j.target == g.id) res << Row{"Current Job", j.id + ": " + j.command, true};
        v->addWidget(makeSection("Resources", res, content));
        Q_UNUSED(stabilityNote);
    } else if ((m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) && m_ctx.matrix && m_ctx.matrix->record(m_loc.id)) {
        const MatrixRunRecord &r = *m_ctx.matrix->record(m_loc.id);
        const TargetResult *tg = nullptr;
        if (m_loc.kind == Location::RunTarget)
            for (const auto &x : r.targets) if (x.avd == m_loc.sub) tg = &x;
        v->addWidget(makeSection("Identity", {{"Run", r.id, true, {}, true}, {"Scheme", "Android Matrix"}, {"Targets", QString::number(r.targets.size())}}, content));
        if (tg) {
            v->addWidget(makeSection("Destination", {{"AVD", tg->avd}, {"ABI", tg->abi}, {"Android API", tg->api}, {"Serial", tg->serial.isEmpty() ? "none" : tg->serial, true},
                                                     {"Started By", tg->startedByUs ? "MobileLab" : "already running"}}, content));
            v->addWidget(makeSection("Execution", {{"Started", fmtTime(tg->started)}, {"Finished", fmtTime(tg->finished)}, {"Duration", fmtMs(tg->durationMs)},
                                                   {"Job", tg->jobId.isEmpty() ? "none" : tg->jobId, true}}, content));
            v->addWidget(makeSection("Result", {{"State", runStateName(tg->state), false, tg->state == RunState::Passed ? t.pass : tg->state == RunState::Failed ? t.fail : t.textTertiary},
                                                {"Executed", QString::number(tg->executedSteps())}, {"Failed", QString::number(tg->failedSteps())}}, content));
            v->addWidget(makeSection("Files", {{"Directory", tg->artifactDir, true, {}, true}}, content));
        } else {
            v->addWidget(makeSection("Execution", {{"Started", fmtTime(r.started)}, {"Finished", fmtTime(r.finished)}, {"Duration", fmtMs(r.durationMs())}}, content));
            v->addWidget(makeSection("Result", {{"State", runStateName(r.state), false, r.state == RunState::Passed ? t.pass : r.state == RunState::Failed ? t.fail : t.textTertiary},
                                                {"Executed", QString::number(r.totalSteps())}, {"Failed", QString::number(r.failedSteps())}}, content));
            v->addWidget(makeSection("Files", {{"Directory", r.dir, true, {}, true}, {"Log", r.logPath, true, {}, true}}, content));
        }
    } else if (m_loc.kind == Location::Container && m_ctx.container) {
        const QJsonObject d = m_ctx.container->diagnostics();
        const QJsonObject w = d.value("waydroid").toObject(), a = d.value("arm64").toObject();
        v->addWidget(makeSection("Waydroid", {{"Backend", w.value("backend").toString()}, {"Installed", w.value("available").toBool() ? "yes" : "no"}, {"Initialised", w.value("initialized").toBool() ? "yes" : "no"},
                                              {"Containerised", w.value("containerized").toBool() ? "yes" : "no"}}, content));
        v->addWidget(makeSection("Host", {{"Architecture", a.value("architecture").toString()}, {"AArch64", a.value("aarch64").toBool() ? "yes" : "no"}, {"KVM", a.value("kvm").toBool() ? "yes" : "no"},
                                          {"binder", a.value("binder").toBool() ? "yes" : "no"}, {"Viable", a.value("container_backend_viable").toBool() ? "yes" : "no"}}, content));
    } else {
        // Nothing selected: the host and the lab.
        QVector<Row> host;
        if (m_ctx.runtime) {
            host << Row{"Architecture", m_ctx.runtime->architecture()} << Row{"Kernel", m_ctx.runtime->kernel()};
            host << Row{"KVM", m_ctx.runtime->kvmAvailable() ? "available" : "unavailable", false, m_ctx.runtime->kvmAvailable() ? t.pass : t.warn};
            host << Row{"QEMU", m_ctx.runtime->qemuAvailable() ? "installed" : "not installed"};
            host << Row{"ABIs", m_ctx.runtime->supportedAbis().isEmpty() ? "no system images" : m_ctx.runtime->supportedAbis().join(", ")};
        }
        v->addWidget(makeSection("Host", host, content));
        QVector<Row> sdk;
        if (m_ctx.emulator) {
            const auto info = m_ctx.emulator->info();
            sdk << Row{"SDK Root", info.sdkRoot, true, {}, true} << Row{"Emulator", QFileInfo::exists(info.emulatorPath) ? info.emulatorPath : "not found", true};
            sdk << Row{"adb", QFileInfo::exists(info.adbPath) ? info.adbPath : "not found", true};
            sdk << Row{"Images", QString::number(m_ctx.emulator->installedSystemImages().size())};
        }
        v->addWidget(makeSection("Android SDK", sdk, content));
        QVector<Row> lab;
        if (m_ctx.runtime) lab << Row{"Targets", QString("%1, %2 running").arg(m_ctx.runtime->targets().size()).arg(m_ctx.runtime->runningCount())};
        if (m_ctx.scheduler) lab << Row{"Capacity", QString("%1 units, %2 CPU slots").arg(m_ctx.scheduler->capacity()).arg(m_ctx.scheduler->cpuSlots())};
        if (m_ctx.api) lab << Row{"REST API", m_ctx.api->isListening() ? QString("http://127.0.0.1:%1").arg(m_ctx.api->port()) : "not listening", true, m_ctx.api->isListening() ? t.pass : t.fail, m_ctx.api->isListening()};
        if (m_ctx.artifacts) lab << Row{"Artifacts", m_ctx.artifacts->root(), true, {}, true};
        if (m_ctx.matrix && !m_ctx.matrix->configSource().isEmpty()) lab << Row{"Matrix Config", m_ctx.matrix->configSource(), true};
        v->addWidget(makeSection("MobileLab", lab, content));
    }
    v->addStretch();
    const int scroll = m_attr->verticalScrollBar()->value();
    content->setAutoFillBackground(false);
    m_attr->setWidget(content);
    m_attr->verticalScrollBar()->setValue(scroll);
}

void Inspector::rebuildHistory() {
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 6, 0, 12);
    v->setSpacing(0);
    QVector<ActivityEntry> entries;
    if (m_ctx.activity) {
        QStringList needles;
        if (m_loc.kind == Location::Target) needles << m_loc.id;
        else if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) {
            needles << m_loc.id;
            if (m_loc.kind == Location::RunTarget) needles << m_loc.sub;
        }
        const auto &all = m_ctx.activity->entries();
        for (int i = all.size() - 1; i >= 0 && entries.size() < 200; --i) {
            bool ok = needles.isEmpty();
            for (const auto &n : needles) if (!n.isEmpty() && all[i].text.contains(n)) ok = true;
            if (ok) entries << all[i];
        }
    }
    // Runs contribute their own events (step results) even before the activity log saw them.
    if (m_loc.kind == Location::RunTarget && m_ctx.matrix && m_ctx.matrix->record(m_loc.id)) {
        for (const auto &tg : m_ctx.matrix->record(m_loc.id)->targets)
            if (tg.avd == m_loc.sub)
                for (int i = tg.steps.size() - 1; i >= 0; --i)
                    if (tg.steps[i].state != RunState::Pending) {
                        ActivityEntry e{tg.finished.isValid() ? tg.finished.toLocalTime() : QDateTime::currentDateTime(), "step", tg.steps[i].name + ": " + runStateName(tg.steps[i].state) + (tg.steps[i].message.isEmpty() ? "" : " - " + tg.steps[i].message)};
                        entries << e;
                    }
    }
    if (entries.isEmpty()) {
        auto *l = new ThemedLabel("No events for this selection yet.", 12, QFont::Normal, ThemedLabel::Role::Tertiary, content);
        l->setContentsMargins(14, 10, 14, 0);
        v->addWidget(l);
    }
    for (const auto &e : entries) {
        auto *row = new QWidget(content);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(12, 3, 12, 3);
        h->setSpacing(8);
        auto *time = new ThemedLabel(e.time.toString("HH:mm:ss"), 11, QFont::Normal, ThemedLabel::Role::Tertiary, row, true);
        time->setFixedWidth(58);
        time->setAlignment(Qt::AlignTop);
        h->addWidget(time);
        auto *txt = new ThemedLabel("[" + e.source + "] " + e.text, 12, QFont::Normal, ThemedLabel::Role::Text, row);
        txt->setWordWrap(true);
        h->addWidget(txt, 1);
        v->addWidget(row);
    }
    v->addStretch();
    content->setAutoFillBackground(false);
    m_hist->setWidget(content);
}

void Inspector::rebuildHelp() {
    QVector<QPair<QString, QString>> blocks;
    if (m_loc.kind == Location::Target && m_ctx.runtime && m_ctx.runtime->target(m_loc.id)) {
        const AndroidTarget &g = *m_ctx.runtime->target(m_loc.id);
        blocks << QPair<QString, QString>{g.id, QString("Android virtual device, API %1, %2 image (%3). ").arg(g.api, g.arch, g.stability) +
                                                 (g.stability == "preferred" ? "x86_64 is the preferred path: SDK images and emulator acceleration are the most mature."
                                                  : g.stability == "fundamental" ? "ARM64 is enabled but fundamental: image availability and runtime compatibility are stricter than x86_64."
                                                                                 : "This ABI is limited: expect gaps.")};
        if (g.state == "running") blocks << QPair<QString, QString>{"Running", QString("The emulator is attached to adb as %1. You can refresh the preview, take a screenshot, run the ABI shell probe, or stop it with Ctrl+.").arg(g.serial)};
        else if (g.state == "booting") blocks << QPair<QString, QString>{"Booting", "The emulator process is up but Android has not finished booting. The preview appears when sys.boot_completed is set."};
        else blocks << QPair<QString, QString>{"Stopped", "Start the device from the context menu or the canvas bar, or run the matrix: it boots stopped targets itself and shuts down the ones it started."};
        if (!m_ctx.runtime->kvmAvailable()) blocks << QPair<QString, QString>{"KVM", "/dev/kvm is missing, so this emulator would run without hardware acceleration and boot slowly."};
    } else if ((m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) && m_ctx.matrix && m_ctx.matrix->record(m_loc.id)) {
        const MatrixRunRecord &r = *m_ctx.matrix->record(m_loc.id);
        blocks << QPair<QString, QString>{"Test run", "A matrix run boots each selected target and runs six checks as test cases: boot, abi, api, logcat, screenshot and shutdown. Failed checks list a remedy in the Issues navigator."};
        if (r.state == RunState::Failed) blocks << QPair<QString, QString>{"This run failed", "Open the Logs tab to see the failing assertion line, or Export JUnit to feed the result to CI."};
        if (r.state == RunState::Running) blocks << QPair<QString, QString>{"Running", "Stop cancels every queued and running target and shuts down emulators this run started (Ctrl+.)."};
        if (r.state == RunState::Cancelled) blocks << QPair<QString, QString>{"Cancelled", "The run was stopped or interrupted; unfinished checks are skipped."};
    } else if (m_loc.kind == Location::Container) {
        blocks << QPair<QString, QString>{"Waydroid", "Waydroid runs Android in a Linux container without virtualisation. It needs an ARM64 host with binder and cgroups, and is experimental here."};
    } else {
        blocks << QPair<QString, QString>{"MobileLab Android", "Select a virtual device, a run or a report to see its attributes, history and help. Ctrl+R runs the matrix on the destination shown in the toolbar capsule."};
        blocks << QPair<QString, QString>{"Layout", "Ctrl+0 hides the navigator, Ctrl+Alt+0 the inspector and Ctrl+Shift+Y the debug area. Drag a divider past its minimum to collapse it."};
    }
    auto *page = paragraphPage(blocks, nullptr);
    page->setAutoFillBackground(false);
    m_help->setWidget(page);
}
