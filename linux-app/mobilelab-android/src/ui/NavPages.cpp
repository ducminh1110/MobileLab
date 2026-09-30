#include "NavPages.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QFile>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QToolTip>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "ActivityLog.h"
#include "AndroidContainerRuntime.h"
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ArtifactCollector.h"
#include "GlassMenu.h"
#include "HostMetrics.h"
#include "Icons.h"
#include "Issues.h"
#include "MatrixExecutor.h"
#include "ResourceScheduler.h"
#include "UiUtil.h"

namespace {

QColor folderTint() { return Ui::mix(tk().folder, tk().sidebar, tk().dark ? 0.15 : 0.40); }

QStandardItem *makeItem(const QString &text, const QString &id, const QString &kind, const QString &icon = {}, const QColor &iconColor = {}) {
    auto *it = new QStandardItem(text);
    it->setEditable(false);
    it->setData(id, NavRole::Id);
    it->setData(kind, NavRole::Kind);
    if (!icon.isEmpty()) it->setData(icon, NavRole::Icon);
    if (iconColor.isValid()) it->setData(iconColor, NavRole::IconColor);
    return it;
}

QString durationText(qint64 ms) {
    if (ms <= 0) return {};
    if (ms < 1000) return QString::number(ms) + " ms";
    const double s = ms / 1000.0;
    if (s < 60) return QString::number(s, 'f', s < 10 ? 1 : 0) + " s";
    const int m = int(s / 60);
    return QString("%1 min %2 s").arg(m).arg(int(s) % 60);
}

QString androidName(const QString &api) {
    static const QHash<QString, QString> names = {
        {"36", "16"}, {"35", "15"}, {"34", "14"}, {"33", "13"}, {"32", "12L"}, {"31", "12"}, {"30", "11"}, {"29", "10"},
        {"28", "9"}, {"27", "8.1"}, {"26", "8.0"}, {"25", "7.1"}, {"24", "7.0"}, {"23", "6.0"}, {"22", "5.1"}, {"21", "5.0"}};
    if (api == "unknown" || api.isEmpty()) return "Unknown API level";
    return names.contains(api) ? QString("Android %1 (API %2)").arg(names[api], api) : "API " + api;
}

void statusVisual(RunState s, const Tokens &t, QString &icon, QColor &color, QString &status) {
    status.clear();
    switch (s) {
    case RunState::Passed: icon = "checkmark.diamond.fill"; color = t.pass; break;
    case RunState::Failed: icon = "xmark.diamond.fill"; color = t.fail; break;
    case RunState::Running: icon.clear(); status = "spin"; break;
    case RunState::Cancelled: icon = "diamond"; color = t.warn; break;
    case RunState::Skipped: icon = "diamond"; color = t.textTertiary; break;
    case RunState::Pending: icon = "diamond"; color = t.textSecondary; break;
    }
}

QString dayLabel(const QDate &d) {
    const QDate today = QDate::currentDate();
    if (d == today) return "Today";
    if (d == today.addDays(-1)) return "Yesterday";
    return QLocale().toString(d, "d MMM yyyy");
}

QString runTitle(const MatrixRunRecord &r) {
    return "Matrix " + r.started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}

QVariant loc(Location::Kind k, const QString &id, const QString &sub = {}, const QString &tab = {}, int line = -1) {
    Location l;
    l.kind = k;
    l.id = id;
    l.sub = sub;
    l.tab = tab;
    l.line = line;
    return QVariant::fromValue(l);
}

}  // namespace

// --- NavPage -----------------------------------------------------------------------------------------

NavPage::NavPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    m_tree = new NavTree(this);
    m_filter = new FilterBar(this);
    connect(m_tree, &NavTree::locationRequested, this, &NavPage::locationRequested);
    connect(m_tree, &NavTree::previewRequested, this, &NavPage::previewRequested);
    connect(m_filter, &FilterBar::textChanged, m_tree, &NavTree::setFilterText);
    connect(&Theme::instance(), &Theme::changed, this, [this] { refresh(); });
}

void NavPage::buildBottomBar(QWidget *leading) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(m_tree, 1);
    m_bottom = new QWidget(this);
    auto *h = new QHBoxLayout(m_bottom);
    h->setContentsMargins(8, 0, 8, 6);
    h->setSpacing(4);
    if (leading) h->addWidget(leading);
    h->addWidget(m_filter, 1);
    m_bottom->setFixedHeight(Metrics::filterBar + 8);
    v->addWidget(m_bottom);
    setFocusProxy(m_tree);
}

// --- DevicesPage -------------------------------------------------------------------------------------

DevicesPage::DevicesPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    auto *plus = new IconButton("plus", this);
    plus->setFixedButtonSize(28, 28);
    plus->setTint(tk().textSecondary);
    Ui::setTip(plus, "New Virtual Device", QKeySequence("Ctrl+N"));
    connect(plus, &QAbstractButton::clicked, this, [this] { emit action("new-avd", {}); });
    connect(&Theme::instance(), &Theme::changed, plus, [plus] { plus->setTint(tk().textSecondary); });
    buildBottomBar(plus);
    m_filter->setPlaceholder("Filter");
    auto *running = m_filter->addToggle("clock", "Show only running targets");
    Q_UNUSED(running);
    connect(m_filter, &FilterBar::toggled, this, [this](int i, bool on) {
        if (i == 0) { m_runningOnly = on; refresh(); }
    });
    m_tree->setEmpty("iphone", "No virtual devices", "Create an Android Virtual Device, or install the Android SDK command line tools and system images.", "Create Virtual Device");
    connect(m_tree, &NavTree::emptyActionTriggered, this, [this] { emit action("new-avd", {}); });
    connect(m_tree, &NavTree::contextRequested, this, &DevicesPage::showMenu);
    m_tree->setAccessibleName("Devices navigator");
    refresh();
}

QString DevicesPage::summary() const {
    return m_ctx.runtime ? QString("%1 targets, %2 running").arg(m_ctx.runtime->targets().size()).arg(m_ctx.runtime->runningCount()) : QString();
}

void DevicesPage::refresh() {
    if (!m_ctx.runtime) return;
    const Tokens &t = tk();
    const auto targets = m_ctx.runtime->targets();
    m_tree->rebuild([&](QStandardItemModel &m) {
        // API level (descending) > ABI (x86_64 first) > target
        QMap<int, QMap<QString, QList<AndroidTarget>>> tree;
        QMap<int, QString> apiOf;
        for (const auto &tg : targets) {
            if (m_runningOnly && tg.state != "running" && tg.state != "booting") continue;
            bool ok = false;
            int api = tg.api.toInt(&ok);
            if (!ok) api = -1;
            tree[api][tg.arch] << tg;
            apiOf[api] = tg.api;
        }
        if (!tree.isEmpty()) {
            auto *root = makeItem("Android Virtual Devices", "root:avd", "group", "folder.fill", folderTint());
            root->setData(true, NavRole::Expand);
            root->setData(true, NavRole::Bold);
            m.appendRow(root);
            for (auto api = tree.end(); api != tree.begin();) {
                --api;
                auto *apiItem = makeItem(androidName(apiOf[api.key()]), "api:" + apiOf[api.key()], "api", "folder.fill", folderTint());
                apiItem->setData(true, NavRole::Expand);
                root->appendRow(apiItem);
                QStringList abis = api.value().keys();
                std::stable_sort(abis.begin(), abis.end(), [](const QString &a, const QString &b) {
                    auto rank = [](const QString &s) { return s == "x86_64" ? 0 : s.contains("arm64") ? 1 : 2; };
                    return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
                });
                for (const auto &abi : abis) {
                    const auto &list = api.value()[abi];
                    auto *abiItem = makeItem(abi, "abi:" + apiOf[api.key()] + ":" + abi, "abi", "folder.fill", folderTint());
                    abiItem->setData(list.first().stability, NavRole::Sub);
                    abiItem->setData(true, NavRole::Expand);
                    apiItem->appendRow(abiItem);
                    for (const auto &tg : list) {
                        const bool tablet = tg.device.contains("tablet", Qt::CaseInsensitive) || tg.id.contains("tablet", Qt::CaseInsensitive);
                        auto *it = makeItem(tg.id, tg.id, "target", tablet ? "ipad" : "iphone");
                        it->setData(tg.state, NavRole::Status);
                        it->setData(tg.tag, NavRole::Trailing);
                        it->setData(loc(Location::Target, tg.id), NavRole::Loc);
                        abiItem->appendRow(it);
                    }
                }
            }
        }
        if (m_ctx.container && (m_ctx.containerProbed || !m_ctx.container)) {
            auto *root = makeItem("Containers", "root:containers", "group", "folder.fill", folderTint());
            root->setData(true, NavRole::Expand);
            root->setData(true, NavRole::Bold);
            const QJsonObject d = m_ctx.container->diagnostics().value("waydroid").toObject();
            auto *w = makeItem("Waydroid", "container:waydroid", "container", "shippingbox");
            w->setData(d.value("available").toBool() ? "installed" : "not installed", NavRole::Trailing);
            w->setData(d.value("available").toBool() ? false : true, NavRole::Dim);
            w->setData(loc(Location::Container, "waydroid"), NavRole::Loc);
            root->appendRow(w);
            m.appendRow(root);
        }
    });
}

void DevicesPage::reveal(const Location &l) {
    if (l.kind == Location::Target) m_tree->selectId(l.id);
    else if (l.kind == Location::Container) m_tree->selectId("container:waydroid");
    else m_tree->clearSelectionSilently();
}

void DevicesPage::showMenu(const QString &id, const QPoint &pos) {
    const AndroidTarget *t = m_ctx.runtime ? m_ctx.runtime->target(id) : nullptr;
    if (!t) return;
    GlassMenu menu(this);
    auto add = [&](const QString &text, const QString &name, bool enabled = true) {
        QAction *a = menu.addAction(text);
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, [this, name, id] { emit action(name, id); });
    };
    const bool running = t->state == "running", booting = t->state == "booting";
    add("Start", "start", t->state == "stopped");
    add("Stop", "stop", running || booting);
    add("Restart", "restart", running);
    menu.addSeparator();
    add("Run Tests on This Target", "run-here");
    add("Take Screenshot", "screenshot", running);
    add("Run ABI Shell Probe", "probe", running);
    menu.addSeparator();
    add("Copy Name", "copy-name");
    menu.exec(pos);
}

// --- TestsPage ---------------------------------------------------------------------------------------

TestsPage::TestsPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    buildBottomBar();
    m_filter->addToggle("clock", "Show only the last 24 hours");
    m_filter->addToggle("xmark.diamond.fill", "Show only failures");
    connect(m_filter, &FilterBar::toggled, this, [this](int i, bool on) {
        (i == 0 ? m_recentOnly : m_failedOnly) = on;
        refresh();
    });
    m_tree->setEmpty("diamond", "No test runs yet", "Run the matrix to boot each target and check its ABI, API level, logcat and screenshot.", "Run Matrix");
    connect(m_tree, &NavTree::emptyActionTriggered, this, [this] { emit action("run", {}); });
    m_tree->setAccessibleName("Tests navigator");
    refresh();
}

void TestsPage::refresh() {
    if (!m_ctx.matrix) return;
    const Tokens &t = tk();
    const auto &records = m_ctx.matrix->records();
    m_tree->rebuild([&](QStandardItemModel &m) {
        QStandardItem *root = nullptr;
        for (int i = records.size() - 1; i >= 0; --i) {
            const auto &r = records[i];
            if (m_failedOnly && r.state != RunState::Failed) continue;
            if (m_recentOnly && r.started.toLocalTime() < QDateTime::currentDateTime().addDays(-1)) continue;
            if (!root) {
                root = makeItem("Runs", "root:runs", "group", "folder.fill", folderTint());
                root->setData(true, NavRole::Expand);
                root->setData(true, NavRole::Bold);
                m.appendRow(root);
            }
            QString icon, status;
            QColor col;
            statusVisual(r.state, t, icon, col, status);
            auto *run = makeItem(runTitle(r), "run:" + r.id, "run", icon, col);
            run->setData(status, NavRole::Status);
            run->setData(durationText(r.durationMs()), NavRole::Trailing);
            run->setData(r.state == RunState::Failed ? QString("%1 failed").arg(r.failedSteps()) : QString(), NavRole::Sub);
            run->setData(i == records.size() - 1 || r.state == RunState::Failed || r.state == RunState::Running, NavRole::Expand);
            run->setData(loc(Location::Run, r.id, {}, "summary"), NavRole::Loc);
            root->appendRow(run);
            for (const auto &tg : r.targets) {
                if (m_failedOnly && tg.state != RunState::Failed) continue;
                statusVisual(tg.state, t, icon, col, status);
                auto *suite = makeItem(tg.avd, "suite:" + r.id + "/" + tg.avd, "suite", icon, col);
                suite->setData(status, NavRole::Status);
                suite->setData(durationText(tg.durationMs), NavRole::Trailing);
                suite->setData(tg.abi, NavRole::Sub);
                suite->setData(tg.state == RunState::Failed || tg.state == RunState::Running, NavRole::Expand);
                suite->setData(loc(Location::RunTarget, r.id, tg.avd, "tests"), NavRole::Loc);
                run->appendRow(suite);
                for (const auto &s : tg.steps) {
                    if (m_failedOnly && s.state != RunState::Failed) continue;
                    statusVisual(s.state, t, icon, col, status);
                    auto *c = makeItem(s.name, "case:" + r.id + "/" + tg.avd + "/" + s.name, "case", icon, col);
                    c->setData(status, NavRole::Status);
                    c->setData(s.state == RunState::Running ? QString() : durationText(s.durationMs), NavRole::Trailing);
                    c->setData(s.state == RunState::Skipped, NavRole::Dim);
                    c->setData(loc(Location::RunTarget, r.id, tg.avd, "logs", s.state == RunState::Failed && s.failLine >= 0 ? s.failLine : s.logLine), NavRole::Loc);
                    suite->appendRow(c);
                }
            }
        }
    });
}

void TestsPage::reveal(const Location &l) {
    if (l.kind == Location::Run) m_tree->selectId("run:" + l.id);
    else if (l.kind == Location::RunTarget) m_tree->selectId("suite:" + l.id + "/" + l.sub);
    else m_tree->clearSelectionSilently();
}

// --- IssuesPage --------------------------------------------------------------------------------------

IssuesPage::IssuesPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    buildBottomBar();
    m_filter->addToggle("xmark.diamond.fill", "Show only errors");
    connect(m_filter, &FilterBar::toggled, this, [this](int, bool on) { m_errorsOnly = on; refresh(); });
    m_tree->setEmpty("checkmark.diamond.fill", "No Issues", "Every probe passed and the latest runs have no failed targets.");
    m_tree->setAccessibleName("Issues navigator");
    refresh();
}

void IssuesPage::refresh() {
    const Tokens &t = tk();
    const QVector<Issue> issues = collectIssues(m_ctx);
    int errors = 0, warnings = 0;
    for (const auto &i : issues) {
        if (i.severity == Issue::Error) ++errors;
        else if (i.severity == Issue::Warning) ++warnings;
    }
    m_count = errors + warnings;
    m_summary = m_count ? QString("%1 errors, %2 warnings").arg(errors).arg(warnings) : "No issues";
    m_tree->rebuild([&](QStandardItemModel &m) {
        QMap<QString, QMap<QString, QVector<Issue>>> grouped;   // category > group
        QStringList categoryOrder;
        for (const auto &i : issues) {
            if (m_errorsOnly && i.severity != Issue::Error) continue;
            if (!categoryOrder.contains(i.category)) categoryOrder << i.category;
            grouped[i.category][i.group] << i;
        }
        for (const auto &cat : categoryOrder) {
            auto *ci = makeItem(cat, "cat:" + cat, "group", "folder.fill", folderTint());
            ci->setData(true, NavRole::Expand);
            ci->setData(true, NavRole::Bold);
            m.appendRow(ci);
            const auto &groups = grouped[cat];
            for (auto g = groups.begin(); g != groups.end(); ++g) {
                QStandardItem *parent = ci;
                if (g.key() != cat && groups.size() + g.value().size() > 0) {
                    auto *gi = makeItem(g.key(), "grp:" + cat + ":" + g.key(), "group", "doc.text");
                    gi->setData(true, NavRole::Expand);
                    ci->appendRow(gi);
                    parent = gi;
                }
                for (const auto &i : g.value()) {
                    const QString icon = i.severity == Issue::Error ? "xmark.diamond.fill" : i.severity == Issue::Warning ? "exclamationmark.triangle" : "info.circle";
                    const QColor col = i.severity == Issue::Error ? t.fail : i.severity == Issue::Warning ? t.warn : t.accent;
                    auto *it = makeItem(i.title, "issue:" + i.id, "issue", icon, col);
                    it->setData(i.detail, NavRole::Sub);
                    Location l = i.where;
                    if (i.category == "Environment") { l.kind = Location::Settings; l.id = "environment"; }
                    it->setData(QVariant::fromValue(l), NavRole::Loc);
                    it->setData(i.remedy, Qt::ToolTipRole);
                    parent->appendRow(it);
                }
            }
        }
    });
    emit countChanged(errors, warnings);
}

// --- FindPage ----------------------------------------------------------------------------------------

FindPage::FindPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *top = new QWidget(this);
    auto *th = new QVBoxLayout(top);
    th->setContentsMargins(8, 0, 8, 4);
    th->setSpacing(4);
    m_filter->setParent(top);
    m_filter->setPlaceholder("Search");
    th->addWidget(m_filter);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(4, 0, 4, 0);
    auto *scopeLabel = new QLabel("Scope", top);
    scopeLabel->setFont(Theme::instance().ui(11, QFont::DemiBold));
    row->addWidget(scopeLabel);
    m_scope = new PopupButton(top);
    m_scope->setFlat(true);
    m_scope->setItems({"Targets", "Runs and Tests", "Run Logs"}, 2);
    m_scope->setAccessibleName("Search scope");
    row->addWidget(m_scope);
    row->addStretch();
    th->addLayout(row);
    v->addWidget(top);
    m_status = new QLabel(this);
    m_status->setFont(Theme::instance().ui(11));
    m_status->setContentsMargins(14, 2, 8, 4);
    v->addWidget(m_status);
    v->addWidget(m_tree, 1);
    disconnect(m_filter, &FilterBar::textChanged, m_tree, &NavTree::setFilterText);
    m_debounce.setParent(this);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &FindPage::search);
    connect(m_filter, &FilterBar::textChanged, this, [this] { m_debounce.start(); });
    connect(m_scope, &PopupButton::currentChanged, this, [this] { search(); });
    connect(m_filter->edit(), &QLineEdit::returnPressed, this, &FindPage::search);
    m_tree->setEmpty("magnifyingglass", "Search MobileLab", "Type to search targets, test runs, or the log of every run.");
    m_tree->setAccessibleName("Find navigator");
    setFocusProxy(m_filter->edit());
    auto pal = [this] {
        QPalette p = m_status->palette();
        p.setColor(QPalette::WindowText, tk().textSecondary);
        m_status->setPalette(p);
    };
    pal();
    connect(&Theme::instance(), &Theme::changed, this, pal);
}

void FindPage::setQuery(const QString &q) {
    m_filter->edit()->setText(q);
    search();
}

void FindPage::search() {
    const QString q = m_filter->text().trimmed();
    const Tokens &t = tk();
    if (q.isEmpty()) {
        m_tree->rebuild([](QStandardItemModel &) {});
        m_tree->setFilterText({});
        m_status->clear();
        return;
    }
    int matches = 0, groups = 0;
    bool truncated = false;
    const int scope = m_scope->currentIndex();
    m_tree->setFilterText({});
    m_tree->rebuild([&](QStandardItemModel &m) {
        if (scope == 0 && m_ctx.runtime) {
            for (const auto &tg : m_ctx.runtime->targets()) {
                const QVector<QPair<QString, QString>> fields = {{"name", tg.id}, {"API level", tg.api}, {"ABI", tg.arch}, {"system image", tg.tag},
                                                                 {"hardware profile", tg.device}, {"state", tg.state}, {"stability", tg.stability}, {"tags", tg.tags.join(", ")}};
                QStandardItem *g = nullptr;
                for (const auto &f : fields) {
                    if (!f.second.contains(q, Qt::CaseInsensitive)) continue;
                    if (!g) {
                        g = makeItem(tg.id, "find:t:" + tg.id, "target", "iphone");
                        g->setData(true, NavRole::Expand);
                        g->setData(loc(Location::Target, tg.id), NavRole::Loc);
                        m.appendRow(g);
                        ++groups;
                    }
                    auto *c = makeItem(f.first + ": " + f.second, "find:t:" + tg.id + ":" + f.first, "match");
                    c->setData(loc(Location::Target, tg.id), NavRole::Loc);
                    g->appendRow(c);
                    ++matches;
                }
            }
        } else if (scope == 1 && m_ctx.matrix) {
            const auto &recs = m_ctx.matrix->records();
            for (int i = recs.size() - 1; i >= 0; --i) {
                const auto &r = recs[i];
                QStandardItem *g = nullptr;
                auto ensure = [&]() {
                    if (g) return;
                    g = makeItem(runTitle(r), "find:r:" + r.id, "run", "doc.text");
                    g->setData(true, NavRole::Expand);
                    g->setData(loc(Location::Run, r.id, {}, "summary"), NavRole::Loc);
                    m.appendRow(g);
                    ++groups;
                };
                if (r.id.contains(q, Qt::CaseInsensitive)) { ensure(); ++matches; }
                for (const auto &tg : r.targets) {
                    for (const auto &s : tg.steps) {
                        const QString line = tg.avd + " " + s.name + " " + runStateName(s.state) + " " + s.message;
                        if (!line.contains(q, Qt::CaseInsensitive)) continue;
                        ensure();
                        auto *c = makeItem(tg.avd + " / " + s.name, "find:s:" + r.id + "/" + tg.avd + "/" + s.name, "match");
                        c->setData(runStateName(s.state) + (s.message.isEmpty() ? "" : ": " + s.message), NavRole::Sub);
                        c->setData(loc(Location::RunTarget, r.id, tg.avd, "logs", s.logLine), NavRole::Loc);
                        g->appendRow(c);
                        ++matches;
                    }
                }
            }
        } else if (scope == 2 && m_ctx.matrix) {
            const auto &recs = m_ctx.matrix->records();
            int files = 0;
            for (int i = recs.size() - 1; i >= 0 && files < 30 && !truncated; --i, ++files) {
                const auto &r = recs[i];
                QFile f(r.logPath);
                if (!f.open(QIODevice::ReadOnly)) continue;
                const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
                QStandardItem *g = nullptr;
                for (int n = 0; n < lines.size(); ++n) {
                    if (!lines[n].contains(q, Qt::CaseInsensitive)) continue;
                    if (matches >= 400) { truncated = true; break; }
                    if (!g) {
                        g = makeItem(runTitle(r) + "  run.log", "find:l:" + r.id, "run", "doc.text");
                        g->setData(true, NavRole::Expand);
                        g->setData(loc(Location::Run, r.id, {}, "logs"), NavRole::Loc);
                        m.appendRow(g);
                        ++groups;
                    }
                    auto *c = makeItem(lines[n].trimmed(), QString("find:l:%1:%2").arg(r.id).arg(n), "match");
                    c->setData(QString::number(n + 1), NavRole::Trailing);
                    c->setData(loc(Location::Run, r.id, {}, "logs", n), NavRole::Loc);
                    g->appendRow(c);
                    ++matches;
                }
            }
        }
    });
    m_tree->setFilterText(q);   // highlights the matches in the rows
    m_status->setText(matches ? QString("%1 %2 in %3 %4%5").arg(matches).arg(matches == 1 ? "match" : "matches").arg(groups).arg(groups == 1 ? "group" : "groups").arg(truncated ? " (first 400 shown)" : "")
                              : "No matches");
    Q_UNUSED(t);
}

// --- DebugPage ---------------------------------------------------------------------------------------

struct GaugeRowData {
    QString icon, label, value, tip;
    qreal fraction = -1;
    QVector<qreal> history;   // recent samples 0..1, drawn as a mini bar chart under the label
};

class GaugeColumn : public QWidget {
    Q_OBJECT
public:
    explicit GaugeColumn(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        m_info = new IconButton("info.circle", this);
        m_info->setFixedButtonSize(22, 22);
        m_info->setGlyphSize(15);
        m_info->setTint(tk().accent);
        Ui::setTip(m_info, "Show host details in the inspector");
        m_pause = new IconButton("pause.circle", this);
        m_pause->setFixedButtonSize(22, 22);
        m_pause->setGlyphSize(15);
        m_pause->setCheckable(true);
        m_pause->setTint(tk().textSecondary);
        Ui::setTip(m_pause, "Pause sampling");
        connect(&Theme::instance(), &Theme::changed, this, [this] { m_info->setTint(tk().accent); m_pause->setTint(tk().textSecondary); update(); });
        setFixedHeight(headerH + 7 * rowH + 6);
    }
    IconButton *infoButton() const { return m_info; }
    IconButton *pauseButton() const { return m_pause; }
    void setTitle(const QString &name, const QString &pid) { m_name = name; m_pid = pid; update(); }
    void setRows(const QVector<GaugeRowData> &r) {
        if (r.size() != m_rows.size()) setFixedHeight(headerH + r.size() * rowH + 6);
        m_rows = r;
        update();
    }
    static constexpr int headerH = 30, rowH = 34;

protected:
    void resizeEvent(QResizeEvent *) override {
        m_pause->move(width() - 8 - 22, 4);
        m_info->move(width() - 8 - 22 - 24, 4);
    }
    void paintEvent(QPaintEvent *) override {
        if (Glass::suppressed()) return;
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Icons::paint(&p, "mobilelab.logo", QRectF(14, 7, 16, 16), t.accent);
        p.setFont(Theme::instance().ui(13, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(36, 0, width() - 110, headerH), Qt::AlignVCenter | Qt::AlignLeft, m_name);
        const int nameW = QFontMetrics(p.font()).horizontalAdvance(m_name);
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textSecondary);
        p.drawText(QRect(36 + nameW + 8, 0, 100, headerH), Qt::AlignVCenter | Qt::AlignLeft, m_pid);
        int y = headerH;
        for (const auto &r : m_rows) {
            p.setPen(Ui::withAlpha(t.text, 22));
            p.drawLine(14, y, width() - 14, y);
            Icons::paint(&p, r.icon, QRectF(14, y + 6, 16, 16), t.textSecondary);
            p.setFont(Theme::instance().ui(13));
            p.setPen(t.text);
            p.drawText(QRect(36, y + 2, width() - 120, 22), Qt::AlignVCenter | Qt::AlignLeft, r.label);
            p.setPen(t.textSecondary);
            p.drawText(QRect(width() - 8 - 130, y + 2, 122, 22), Qt::AlignVCenter | Qt::AlignRight, r.value);
            if (r.fraction >= 0) {
                // recent samples as thin vertical bars under the label (the newest on the right), like the reference
                p.setPen(Qt::NoPen);
                p.setBrush(t.accent);
                const QVector<qreal> h = r.history.isEmpty() ? QVector<qreal>{r.fraction} : r.history;
                const int n = h.size();
                for (int i = 0; i < n; ++i) {
                    const qreal v = qBound<qreal>(0.0, h[i], 1.0);
                    const qreal bh = qMax<qreal>(2.0, 9.0 * v);
                    p.drawRect(QRectF(37 + i * 3.0, y + 31 - bh, 2, bh));
                }
            }
            y += rowH;
        }
    }
    bool event(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            const auto *he = static_cast<QHelpEvent *>(e);
            const int idx = (he->pos().y() - headerH) / rowH;
            if (idx >= 0 && idx < m_rows.size() && !m_rows[idx].tip.isEmpty()) {
                QToolTip::showText(he->globalPos(), m_rows[idx].tip, this);
                return true;
            }
        }
        return QWidget::event(e);
    }

private:
    QVector<GaugeRowData> m_rows;
    QString m_name = "MobileLab Android", m_pid;
    IconButton *m_info, *m_pause;
};

DebugPage::DebugPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    m_gauges = new GaugeColumn(this);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(m_gauges);
    v->addWidget(m_tree, 1);
    m_bottom = new QWidget(this);
    auto *h = new QHBoxLayout(m_bottom);
    h->setContentsMargins(8, 0, 8, 6);
    h->addWidget(m_filter, 1);
    m_bottom->setFixedHeight(Metrics::filterBar + 8);
    v->addWidget(m_bottom);
    m_tree->setEmpty("cpu", "Nothing is running", "Queued and running matrix jobs appear here as threads.");
    m_tree->setAccessibleName("Debug navigator");
    m_timer.setParent(this);
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &DebugPage::refresh);
    connect(m_gauges->pauseButton(), &QAbstractButton::toggled, this, [this](bool paused) {
        if (paused) m_timer.stop();
        else if (isVisible()) { m_timer.start(); refresh(); }
    });
    connect(m_gauges->infoButton(), &QAbstractButton::clicked, this, [this] { emit action("host-info", {}); });
    if (m_ctx.scheduler) connect(m_ctx.scheduler, &ResourceScheduler::jobChanged, this, [this] { if (isVisible()) refresh(); });
    setFocusProxy(m_tree);
}

void DebugPage::showEvent(QShowEvent *e) {
    NavPage::showEvent(e);
    if (!m_gauges->pauseButton()->isChecked()) {
        m_timer.start();
        if (m_ctx.host) m_ctx.host->sample();
        refresh();
    }
}

void DebugPage::hideEvent(QHideEvent *e) {
    NavPage::hideEvent(e);
    m_timer.stop();
}

void DebugPage::refresh() {
    if (!m_ctx.host) return;
    if (isVisible() && !m_gauges->pauseButton()->isChecked() && sender() == &m_timer) m_ctx.host->sample();
    const HostMetrics &h = *m_ctx.host;
    static double diskMax = 1 << 20, netMax = 1 << 20;
    diskMax = qMax(diskMax, h.diskBytesPerSec());
    netMax = qMax(netMax, h.netBytesPerSec());
    // The artifact store is walked at most every 15 s.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_ctx.artifacts && now - m_artifactStamp > 15000) {
        m_artifactBytes = m_ctx.artifacts->sizeOnDisk();
        m_artifactStamp = now;
    }
    QVector<GaugeRowData> rows;
    const bool ok = h.valid();
    rows.push_back({"cpu", "CPU", ok ? QString::number(h.processCpuPercent(), 'f', 1) + "%" : "...", QString("This process. Host CPU %1%, %2 cores").arg(h.hostCpuPercent(), 0, 'f', 1).arg(h.cores()),
                    ok ? h.processCpuPercent() / (100.0 * h.cores()) : -1.0});
    rows.push_back({"memorychip", "Memory", HostMetrics::formatBytes(double(h.rssBytes())),
                    QString("Resident set of this process. Host: %1 available of %2").arg(HostMetrics::formatBytes(double(h.hostMemAvailable())), HostMetrics::formatBytes(double(h.hostMemTotal()))),
                    h.hostMemTotal() > 0 ? double(h.rssBytes()) / double(h.hostMemTotal()) : -1.0});
    rows.push_back({"internaldrive", "Disk", h.diskAvailable() ? (ok ? HostMetrics::formatRate(h.diskBytesPerSec()) : "...") : "Unavailable", "Read and write throughput of this process (/proc/self/io)",
                    h.diskAvailable() && ok ? h.diskBytesPerSec() / diskMax : -1.0});
    rows.push_back({"network", "Network", h.netAvailable() ? (ok ? HostMetrics::formatRate(h.netBytesPerSec()) : "...") : "Unavailable", "All non-loopback interfaces of the host",
                    h.netAvailable() && ok ? h.netBytesPerSec() / netMax : -1.0});
    rows.push_back({"folder", "Artifacts", m_artifactBytes >= 0 ? HostMetrics::formatBytes(double(m_artifactBytes)) : "...", m_ctx.artifacts ? m_ctx.artifacts->root() : QString(), -1.0});
    if (m_ctx.scheduler) {
        const auto &s = *m_ctx.scheduler;
        rows.push_back({"bolt.horizontal", "Capacity", QString("%1 of %2 units").arg(s.usedCost()).arg(s.capacity()),
                        QString("Scheduler: %1 CPU slots x 2, %2 running, %3 queued").arg(s.cpuSlots()).arg(s.runningJobs().size()).arg(s.queuedJobs().size()),
                        s.capacity() ? double(s.usedCost()) / s.capacity() : -1.0});
    }
    rows.push_back({"cpu", "Host load", QString("%1 (1 min)").arg(h.load1(), 0, 'f', 2), QString("%1 logical cores").arg(h.cores()), qMin(1.0, h.load1() / h.cores())});
    // keep the last 24 samples per row for the mini charts
    if (sender() == &m_timer || m_history.isEmpty()) {
        for (auto &r : rows) {
            auto &hist = m_history[r.label];
            if (r.fraction >= 0) hist.push_back(r.fraction);
            while (hist.size() > 24) hist.removeFirst();
        }
    }
    for (auto &r : rows) r.history = m_history.value(r.label);
    m_gauges->setTitle("MobileLab Android", "PID " + QString::number(h.pid()));
    m_gauges->setRows(rows);

    if (!m_ctx.scheduler) return;
    const auto running = m_ctx.scheduler->runningJobs();
    const auto queued = m_ctx.scheduler->queuedJobs();
    m_tree->rebuild([&](QStandardItemModel &m) {
        int n = 1;
        for (const auto &j : running) {
            auto *th = makeItem(QString("Thread %1").arg(n++), "thread:" + j.id, "thread", "cpu", tk().accent);
            th->setData("Queue: " + j.target, NavRole::Sub);
            th->setData(true, NavRole::Expand);
            th->setData(loc(Location::Target, j.target), NavRole::Loc);
            const QStringList frames = {QString("job %1").arg(j.id), QString("target %1").arg(j.target), QString("command %1").arg(j.command),
                                        QString("attempt %1, cost %2, priority %3").arg(j.attempt).arg(j.cost).arg(j.priority)};
            for (int f = 0; f < frames.size(); ++f) {
                auto *fr = makeItem(QString("%1  %2").arg(f).arg(frames[f]), QString("frame:%1:%2").arg(j.id).arg(f), "frame", "circle", tk().textTertiary);
                fr->setData(loc(Location::Target, j.target), NavRole::Loc);
                th->appendRow(fr);
            }
            m.appendRow(th);
        }
        if (!queued.isEmpty()) {
            auto *q = makeItem(QString("Queued (%1)").arg(queued.size()), "queued", "group", "clock", tk().textSecondary);
            q->setData(true, NavRole::Expand);
            for (const auto &j : queued) {
                auto *it = makeItem(QString("%1 on %2").arg(j.id, j.target), "queued:" + j.id, "queued", "circle", tk().textTertiary);
                it->setData(true, NavRole::Dim);
                it->setData(QString("cost %1").arg(j.cost), NavRole::Trailing);
                q->appendRow(it);
            }
            m.appendRow(q);
        }
    });
}

// --- ReportsPage -------------------------------------------------------------------------------------

ReportsPage::ReportsPage(const AppContext &ctx, QWidget *parent) : NavPage(ctx, parent) {
    buildBottomBar();
    m_filter->addToggle("xmark.diamond.fill", "Show only failed runs");
    connect(m_filter, &FilterBar::toggled, this, [this](int, bool on) { m_failedOnly = on; refresh(); });
    m_tree->setEmpty("doc.text", "No reports", "A report is written for every matrix run, with logs and screenshots.", "Run Matrix");
    connect(m_tree, &NavTree::emptyActionTriggered, this, [this] { emit action("run", {}); });
    m_tree->setAccessibleName("Reports navigator");
    refresh();
}

void ReportsPage::refresh() {
    if (!m_ctx.matrix) return;
    const Tokens &t = tk();
    const auto &records = m_ctx.matrix->records();
    m_tree->rebuild([&](QStandardItemModel &m) {
        QMap<QDate, QStandardItem *> days;
        for (int i = records.size() - 1; i >= 0; --i) {
            const auto &r = records[i];
            if (m_failedOnly && r.state != RunState::Failed) continue;
            const QDate d = r.started.toLocalTime().date();
            QStandardItem *day = days.value(d);
            if (!day) {
                day = makeItem(dayLabel(d), "day:" + d.toString(Qt::ISODate), "group");
                day->setData(true, NavRole::Expand);
                day->setData(true, NavRole::Bold);
                m.appendRow(day);
                days[d] = day;
            }
            QString icon, status;
            QColor col;
            statusVisual(r.state, t, icon, col, status);
            const QString what = r.targets.size() == 1 ? r.targets.first().avd : QString("%1 targets").arg(r.targets.size());
            auto *row = makeItem("Android Matrix on " + what, "report:" + r.id, "report", icon, col);
            row->setData(status, NavRole::Status);
            row->setData(r.started.toLocalTime().toString("h:mm AP") + (r.finished.isValid() ? "  " + durationText(r.durationMs()) : QString()), NavRole::Trailing);
            row->setData(loc(Location::Run, r.id, {}, "summary"), NavRole::Loc);
            day->appendRow(row);
        }
    });
}

void ReportsPage::reveal(const Location &l) {
    if (l.kind == Location::Run || l.kind == Location::RunTarget) m_tree->selectId("report:" + l.id);
    else m_tree->clearSelectionSilently();
}

// --- Navigator ---------------------------------------------------------------------------------------

QString Navigator::tabName(int tab) {
    static const char *names[] = {"Devices", "Tests", "Issues", "Find", "Debug", "Reports"};
    return names[qBound(0, tab, TabCount - 1)];
}

Navigator::Navigator(const AppContext &ctx, QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_tabs = new NavTabBar(this);
    m_tabs->setTabs({{"iphone", "Devices Navigator", QKeySequence("Ctrl+1")},
                     {"diamond", "Tests Navigator", QKeySequence("Ctrl+2")},
                     {"exclamationmark.triangle", "Issues Navigator", QKeySequence("Ctrl+3")},
                     {"magnifyingglass", "Find Navigator", QKeySequence("Ctrl+4")},
                     {"cpu", "Debug Navigator", QKeySequence("Ctrl+5")},
                     {"doc.text", "Reports Navigator", QKeySequence("Ctrl+6")}});
    auto *tabWrap = new QWidget(this);
    auto *th = new QHBoxLayout(tabWrap);
    th->setContentsMargins(6, 2, 6, 0);
    th->addWidget(m_tabs);
    tabWrap->setFixedHeight(Metrics::navTabBar);
    v->addWidget(tabWrap);
    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack, 1);
    m_pages[Devices] = new DevicesPage(ctx, this);
    m_pages[Tests] = new TestsPage(ctx, this);
    m_pages[Issues] = new IssuesPage(ctx, this);
    m_pages[Find] = new FindPage(ctx, this);
    m_pages[Debug] = new DebugPage(ctx, this);
    m_pages[Reports] = new ReportsPage(ctx, this);
    for (auto *p : m_pages) {
        m_stack->addWidget(p);
        connect(p, &NavPage::locationRequested, this, &Navigator::locationRequested);
        connect(p, &NavPage::previewRequested, this, &Navigator::previewRequested);
        connect(p, &NavPage::action, this, &Navigator::action);
    }
    connect(m_tabs, &NavTabBar::currentChanged, this, [this](int i) {
        m_stack->setCurrentIndex(i);
        emit currentChanged(i);
    });
    connect(issues(), &IssuesPage::countChanged, this, [this](int e, int w) {
        m_tabs->setBadge(Issues, e ? e : w, e ? tk().fail : tk().warn);
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] { issues()->refresh(); });
    setAccessibleName("Navigator");
}

int Navigator::current() const { return m_stack->currentIndex(); }

void Navigator::setCurrent(int tab, bool focusTree) {
    tab = qBound(0, tab, TabCount - 1);
    m_tabs->setCurrent(tab, false);
    m_stack->setCurrentIndex(tab);
    emit currentChanged(tab);
    if (focusTree) m_pages[tab]->focusTarget()->setFocus(Qt::OtherFocusReason);
}

void Navigator::refreshAll() {
    for (auto *p : m_pages)
        if (p != m_pages[Debug] && p != m_pages[Find]) p->refresh();
    if (current() == Debug) m_pages[Debug]->refresh();
}

void Navigator::reveal(const Location &loc) {
    for (auto *p : m_pages) p->reveal(loc);
}

#include "NavPages.moc"
