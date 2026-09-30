#include "EditorArea.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QThread>
#include <QVBoxLayout>
#include "AndroidContainerRuntime.h"
#include "AndroidRuntime.h"
#include "MatrixExecutor.h"
#include "editors/EditorParts.h"
#include "Icons.h"
#include "UiUtil.h"

class ContainerEditor : public QWidget {
    Q_OBJECT
public:
    ContainerEditor(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
        setAccessibleName("Waydroid container");
        m_root = new QVBoxLayout(this);
        m_root->setContentsMargins(0, 0, 0, 0);
        refresh();
        connect(&Theme::instance(), &Theme::changed, this, [this] { refresh(); });
    }
    void refresh() {
        delete m_content;
        m_content = new QWidget(this);
        auto *v = new QVBoxLayout(m_content);
        v->setContentsMargins(36, 30, 36, 30);
        v->setSpacing(6);
        v->addWidget(new ThemedLabel("Waydroid", 24, QFont::DemiBold, ThemedLabel::Role::Text, m_content));
        v->addWidget(new ThemedLabel("Android in a Linux container, without virtualisation. Experimental; needs an ARM64 host with binder and cgroups.", 12, QFont::Normal, ThemedLabel::Role::Secondary, m_content));
        v->addSpacing(12);
        const QJsonObject d = m_ctx.container ? m_ctx.container->diagnostics() : QJsonObject();
        const QJsonObject w = d.value("waydroid").toObject(), a = d.value("arm64").toObject();
        auto row = [&](const QString &k, bool ok, const QString &detail = {}) {
            auto *r = new RowButton(ok ? "checkmark.diamond.fill" : "xmark.diamond.fill", k, {}, detail.isEmpty() ? (ok ? "yes" : "no") : detail, m_content);
            r->setPlainRow(true);
            r->setIconColor(ok ? tk().pass : tk().warn);
            v->addWidget(r);
        };
        row("Waydroid installed", w.value("available").toBool());
        row("Container initialised", w.value("initialized").toBool());
        row("ARM64 host", a.value("aarch64").toBool(), a.value("architecture").toString());
        row("binder device", a.value("binder").toBool());
        row("cgroups", a.value("cgroup_v2_or_mount").toBool());
        row("lxc tools", a.value("lxc_tools").toBool());
        row("KVM (not required)", a.value("kvm").toBool());
        row("Backend viable", a.value("container_backend_viable").toBool());
        auto *h = new QHBoxLayout;
        h->setContentsMargins(0, 14, 0, 0);
        m_start = new PillButton("Start Container", PillButton::Style::Primary, m_content, "play.fill");
        m_stop = new PillButton("Stop Container", PillButton::Style::Secondary, m_content, "stop.fill");
        const bool can = a.value("container_backend_viable").toBool() && w.value("available").toBool();
        m_start->setEnabled(can && !m_busy);
        m_stop->setEnabled(can && !m_busy);
        connect(m_start, &QAbstractButton::clicked, this, [this] { run(true); });
        connect(m_stop, &QAbstractButton::clicked, this, [this] { run(false); });
        h->addWidget(m_start);
        h->addWidget(m_stop);
        h->addStretch();
        v->addLayout(h);
        if (!can) {
            auto *why = new ThemedLabel("Start and Stop are disabled because this host cannot run the container backend (see the checks above).", 12, QFont::Normal, ThemedLabel::Role::Tertiary, m_content);
            why->setWordWrap(true);
            v->addWidget(why);
        }
        v->addStretch();
        while (m_root->count()) delete m_root->takeAt(0);
        m_root->addWidget(m_content);
    }
private:
    void run(bool start) {
        if (!m_ctx.container || m_busy) return;
        m_busy = true;
        refresh();
        // waydroid commands can take a long time: keep them off the UI thread
        QThread *th = QThread::create([c = m_ctx.container, start] { if (start) c->start(); else c->stop(); });
        connect(th, &QThread::finished, this, [this, th] { th->deleteLater(); m_busy = false; refresh(); });
        th->start();
    }
    AppContext m_ctx;
    QVBoxLayout *m_root;
    QWidget *m_content = nullptr;
    PillButton *m_start = nullptr, *m_stop = nullptr;
    bool m_busy = false;
};

EditorArea::EditorArea(const AppContext &ctx, const Actions &a, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_jump = new JumpBar(a.back, a.forward, a.related, a.options, a.add, this);
    v->addWidget(m_jump);
    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack, 1);
    m_welcome = new WelcomeEditor(ctx, this);
    m_target = new TargetEditor(ctx, this);
    m_run = new RunEditor(ctx, this);
    m_container = new ContainerEditor(ctx, this);
    for (QWidget *w : std::initializer_list<QWidget *>{m_welcome, m_target, m_run, m_container}) m_stack->addWidget(w);
    connect(m_jump, &JumpBar::locationRequested, this, &EditorArea::locationRequested);
    connect(m_welcome, &WelcomeEditor::locationRequested, this, &EditorArea::locationRequested);
    connect(m_welcome, &WelcomeEditor::action, this, &EditorArea::action);
    connect(m_target, &TargetEditor::action, this, &EditorArea::action);
    connect(m_run, &RunEditor::locationRequested, this, &EditorArea::locationRequested);
    connect(m_run, &RunEditor::action, this, &EditorArea::action);
    connect(m_ctx.runtime, &AndroidRuntime::targetsChanged, this, [this] {
        if (m_loc.kind == Location::Target) { m_target->refresh(); m_jump->setCrumbs(crumbsFor(m_loc)); }
    });
    connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this] {
        if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) m_jump->setCrumbs(crumbsFor(m_loc));
    });
    setAccessibleName("Editor");
}

void EditorArea::refresh() {
    if (m_loc.kind == Location::Target) m_target->refresh();
    else if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) m_run->refresh();
    else if (m_loc.kind == Location::Welcome) m_welcome->refresh();
    m_jump->setCrumbs(crumbsFor(m_loc));
}

void EditorArea::showLocation(const Location &loc) {
    m_loc = loc;
    switch (loc.kind) {
    case Location::Target: m_target->showTarget(loc.id); m_stack->setCurrentWidget(m_target); break;
    case Location::Run: case Location::RunTarget: m_run->showLocation(loc); m_stack->setCurrentWidget(m_run); break;
    case Location::Container: m_stack->setCurrentWidget(m_container); break;
    default: m_welcome->refresh(); m_stack->setCurrentWidget(m_welcome); break;
    }
    m_jump->setCrumbs(crumbsFor(loc));
}

QVector<JumpBar::Crumb> EditorArea::crumbsFor(const Location &loc) const {
    using Crumb = JumpBar::Crumb;
    const Tokens &t = tk();
    QVector<Crumb> out;
    Crumb root;
    root.text = "MobileLab";
    root.icon = "mobilelab.mark";
    root.iconColor = t.accent;
    root.loc = Location{};
    out << root;
    auto targetLoc = [](const QString &id) { Location l; l.kind = Location::Target; l.id = id; return l; };
    if (loc.kind == Location::Target && m_ctx.runtime->target(loc.id)) {
        const AndroidTarget &g = *m_ctx.runtime->target(loc.id);
        Crumb devices;
        devices.text = "Devices";
        devices.icon = "folder.fill";
        devices.loc = Location{};
        for (const auto &x : m_ctx.runtime->targets()) devices.siblings << JumpBar::Sibling{x.id, "iphone", targetLoc(x.id), x.id == g.id};
        out << devices;
        Crumb api;
        api.text = g.api == "unknown" ? "Unknown API" : "API " + g.api;
        api.icon = "folder.fill";
        QStringList seen;
        for (const auto &x : m_ctx.runtime->targets())
            if (!seen.contains(x.api)) { seen << x.api; api.siblings << JumpBar::Sibling{x.api == "unknown" ? "Unknown API" : "API " + x.api, "folder.fill", targetLoc(x.id), x.api == g.api}; }
        out << api;
        Crumb abi;
        abi.text = g.arch;
        abi.icon = "folder.fill";
        seen.clear();
        for (const auto &x : m_ctx.runtime->targets())
            if (x.api == g.api && !seen.contains(x.arch)) { seen << x.arch; abi.siblings << JumpBar::Sibling{x.arch, "folder.fill", targetLoc(x.id), x.arch == g.arch}; }
        out << abi;
        Crumb dev;
        dev.text = g.id;
        dev.icon = "iphone";
        dev.iconColor = t.textSecondary;
        dev.loc = loc;
        for (const auto &x : m_ctx.runtime->targets())
            if (x.api == g.api && x.arch == g.arch) dev.siblings << JumpBar::Sibling{x.id, "iphone", targetLoc(x.id), x.id == g.id};
        out << dev;
    } else if ((loc.kind == Location::Run || loc.kind == Location::RunTarget) && m_ctx.matrix->record(loc.id)) {
        const MatrixRunRecord &r = *m_ctx.matrix->record(loc.id);
        Crumb reports;
        reports.text = "Reports";
        reports.icon = "folder.fill";
        const auto &recs = m_ctx.matrix->records();
        for (int i = recs.size() - 1; i >= 0 && reports.siblings.size() < 12; --i) {
            Location l; l.kind = Location::Run; l.id = recs[i].id; l.tab = "summary";
            reports.siblings << JumpBar::Sibling{recs[i].started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"), "doc.text", l, recs[i].id == r.id};
        }
        out << reports;
        Crumb run;
        run.text = "Matrix " + r.started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
        run.icon = "doc.text";
        run.iconColor = t.textSecondary;
        run.loc.kind = Location::Run;
        run.loc.id = r.id;
        run.loc.tab = "summary";
        out << run;
        if (loc.kind == Location::RunTarget) {
            Crumb tg;
            tg.text = loc.sub;
            tg.icon = "iphone";
            tg.iconColor = t.textSecondary;
            tg.loc = loc;
            for (const auto &x : r.targets) {
                Location l; l.kind = Location::RunTarget; l.id = r.id; l.sub = x.avd; l.tab = "summary";
                tg.siblings << JumpBar::Sibling{x.avd, "iphone", l, x.avd == loc.sub};
            }
            out << tg;
        }
        Crumb tab;
        const QString cur = loc.tab.isEmpty() ? "summary" : loc.tab;
        tab.text = cur == "tests" ? "Tests" : cur == "logs" ? "Logs" : "Summary";
        tab.icon = "diamond";
        tab.iconColor = t.textSecondary;
        for (const auto &name : {QString("summary"), QString("tests"), QString("logs")}) {
            Location l = loc;
            l.tab = name;
            l.line = -1;
            tab.siblings << JumpBar::Sibling{name == "summary" ? "Summary" : name == "tests" ? "Tests" : "Logs", "diamond", l, name == cur};
        }
        tab.loc = loc;
        out << tab;
    } else if (loc.kind == Location::Container) {
        Crumb c1;
        c1.text = "Containers";
        c1.icon = "folder.fill";
        out << c1;
        Crumb c2;
        c2.text = "Waydroid";
        c2.icon = "shippingbox";
        c2.iconColor = t.textSecondary;
        out << c2;
    }
    return out;
}

#include "EditorArea.moc"
