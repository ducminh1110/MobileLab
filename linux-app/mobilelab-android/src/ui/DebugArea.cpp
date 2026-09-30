#include "DebugArea.h"
#include <QAction>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSplitterHandle>
#include <QVBoxLayout>
#include "ActivityLog.h"
#include "AndroidRuntime.h"
#include "MatrixExecutor.h"
#include "ResourceScheduler.h"
#include "UiUtil.h"

namespace {
class LineHandle : public QSplitterHandle {
public:
    LineHandle(Qt::Orientation o, QSplitter *p) : QSplitterHandle(o, p) { setAccessibleName("Variables and console divider"); }
protected:
    void paintEvent(QPaintEvent *) override {
        if (Glass::suppressed()) return;
        QPainter p(this);
        p.setPen(tk().dark ? Ui::withAlpha(Qt::white, 24) : Ui::withAlpha(Qt::black, 26));
        p.drawLine(width() / 2, 0, width() / 2, height());
    }
};

QString typeName(const QVariant &v) {
    switch (v.typeId()) {
    case QMetaType::Bool: return "Bool";
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: case QMetaType::ULongLong: case QMetaType::Double: return v.typeId() == QMetaType::Double ? "Double" : "Int";
    default: return "String";
    }
}
QString valueText(const QVariant &v) {
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? "true" : "false";
    if (v.typeId() == QMetaType::QString) return "\"" + v.toString() + "\"";
    return v.toString();
}
QString durationText(qint64 ms) { return ms > 0 ? QString::number(ms / 1000.0, 'f', ms < 10000 ? 2 : 1) + " s" : QString("0 s"); }

QStandardItem *var(const QString &name, const QString &value, const QString &type, const QString &id, const QColor &squareColor = {}, const QString &square = {}) {
    auto *it = new QStandardItem(name);
    it->setEditable(false);
    it->setData(id, NavRole::Id);
    it->setData(QString("= %1: %2").arg(value, type), NavRole::Sub);
    it->setData(true, NavRole::SubMono);
    if (!square.isEmpty()) { it->setData(square, NavRole::Square); it->setData(squareColor, NavRole::SquareColor); }
    return it;
}
}

QSplitterHandle *LineSplitter::createHandle() { return new LineHandle(orientation(), this); }

DebugArea::DebugArea(const AppContext &ctx, const Actions &a, QWidget *parent) : QWidget(parent), m_ctx(ctx), m_a(a) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAccessibleName("Debug area");
    const int bh = Metrics::debugBar;
    // --- bar ---
    auto mkBtn = [&](const QString &icon, QAction *act, const QString &tip = {}) {
        auto *b = new IconButton(icon, this);
        b->setFixedButtonSize(26, 22);
        b->setGlyphSize(14);
        b->setTint(tk().textSecondary);
        if (act) b->bindAction(act, tip);
        return b;
    };
    m_toggleBtn = mkBtn("sidebar.bottom", a.toggleDebug);
    m_toggleBtn->setAccentWhenChecked(false);
    m_runBtn = mkBtn("play.fill", a.run, "Run Matrix Again");
    m_stopBtn = mkBtn("stop.fill", a.stop, "Stop");
    m_retryBtn = mkBtn("arrow.clockwise", a.retryFailed, "Run Failed Targets Again");
    m_shotBtn = mkBtn("camera", a.screenshot, "Take Screenshot of Selected Device");
    m_inspectorBtn = mkBtn("sidebar.right", a.toggleInspector);
    m_inspectorBtn->setAccentWhenChecked(false);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        for (auto *b : {m_toggleBtn, m_runBtn, m_stopBtn, m_retryBtn, m_shotBtn, m_inspectorBtn}) b->setTint(tk().textSecondary);
        update();
    });
    // --- body ---
    m_split = new LineSplitter(Qt::Horizontal, this);
    m_vars = new NavTree(m_split);
    m_vars->setCompact(true);
    m_vars->setAccessibleName("Variables view");
    m_vars->setEmpty("cpu", "No variables", "Select a device or a run to inspect its values.");
    m_console = new LogView(LogView::Mode::Console, m_split);
    m_console->setEmptyText("No output yet.");
    m_split->addWidget(m_vars);
    m_split->addWidget(m_console);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({1, 1});
    // --- footer ---
    m_footer = new QWidget(this);
    auto *fh = new QHBoxLayout(m_footer);
    fh->setContentsMargins(0, 0, 0, 0);
    fh->setSpacing(0);
    m_footL = new QWidget(m_footer);
    auto *lh = new QHBoxLayout(m_footL);
    lh->setContentsMargins(8, 3, 8, 3);
    lh->setSpacing(6);
    m_mode = new PopupButton(m_footL);
    m_mode->setFlat(true);
    m_mode->setItems({"Auto", "Failures", "Running"}, 0);
    m_mode->setAccessibleName("Variables style");
    lh->addWidget(m_mode);
    lh->addStretch();
    m_varFilter = new FilterBar(m_footL);
    m_varFilter->setFixedWidth(190);
    m_varFilter->setFixedHeight(26 + 2 * m_varFilter->shadowMargin() - 4);
    m_varFilter->setAccessibleName("Filter variables");
    lh->addWidget(m_varFilter);
    m_footR = new QWidget(m_footer);
    auto *rh = new QHBoxLayout(m_footR);
    rh->setContentsMargins(8, 3, 8, 3);
    rh->setSpacing(6);
    m_output = new PopupButton(m_footR);
    m_output->setFlat(true);
    m_output->setItems({"All Output", "Errors", "Runtime", "Scheduler", "Matrix", "Test Results"}, 0);
    m_output->setAccessibleName("Console output filter");
    rh->addWidget(m_output);
    rh->addStretch();
    m_consoleFilter = new FilterBar(m_footR);
    m_consoleFilter->setFixedWidth(190);
    m_consoleFilter->setFixedHeight(26 + 2 * m_consoleFilter->shadowMargin() - 4);
    m_consoleFilter->setAccessibleName("Filter console");
    rh->addWidget(m_consoleFilter);
    m_trash = new IconButton("trash", m_footR);
    m_trash->setFixedButtonSize(24, 22);
    m_trash->setGlyphSize(14);
    m_trash->setTint(tk().textSecondary);
    m_trash->bindAction(a.clearConsole, "Clear Console");
    rh->addWidget(m_trash);
    m_showVars = new IconButton("sidebar.left", m_footR);
    m_showVars->setFixedButtonSize(24, 22);
    m_showVars->setGlyphSize(14);
    m_showVars->setCheckable(true);
    m_showVars->setChecked(true);
    m_showVars->setTint(tk().textSecondary);
    Ui::setTip(m_showVars, "Show Variables View");
    m_showConsole = new IconButton("sidebar.right", m_footR);
    m_showConsole->setFixedButtonSize(24, 22);
    m_showConsole->setGlyphSize(14);
    m_showConsole->setCheckable(true);
    m_showConsole->setChecked(true);
    m_showConsole->setTint(tk().textSecondary);
    Ui::setTip(m_showConsole, "Show Console");
    rh->addWidget(m_showVars);
    rh->addWidget(m_showConsole);
    fh->addWidget(m_footL);
    fh->addWidget(m_footR);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        for (auto *b : {m_trash, m_showVars, m_showConsole}) b->setTint(tk().textSecondary);
        update();
    });
    connect(m_showVars, &QAbstractButton::toggled, this, [this](bool on) {
        if (!on && !m_showConsole->isChecked()) { m_showVars->setChecked(true); return; }
        m_vars->setVisible(on);
        m_footL->setVisible(on);
        relayout();
    });
    connect(m_showConsole, &QAbstractButton::toggled, this, [this](bool on) {
        if (!on && !m_showVars->isChecked()) { m_showConsole->setChecked(true); return; }
        m_console->setVisible(on);
        m_footR->setVisible(on);
        relayout();
    });
    connect(m_split, &QSplitter::splitterMoved, this, [this] { syncFooter(); update(); });
    connect(m_mode, &PopupButton::currentChanged, this, [this] { refreshVariables(); });
    connect(m_varFilter, &FilterBar::textChanged, this, [this](const QString &t) { m_vars->setFilterText(t); });
    connect(m_output, &PopupButton::currentChanged, this, [this](int i) { m_outputIndex = i; applyFilter(); });
    connect(m_consoleFilter, &FilterBar::textChanged, this, [this](const QString &t) { m_consoleText = t; applyFilter(); m_console->setHighlight(t); });
    connect(a.clearConsole, &QAction::triggered, this, [this] {
        if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) m_console->clear();
        else if (m_ctx.activity) m_ctx.activity->clear();
    });
    // live feeds
    m_coalesce.setParent(this);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(200);
    connect(&m_coalesce, &QTimer::timeout, this, &DebugArea::refreshVariables);
    if (m_ctx.activity) {
        connect(m_ctx.activity, &ActivityLog::added, this, [this](const ActivityEntry &e) {
            if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) return;
            m_console->appendLine(e.line());
        });
        connect(m_ctx.activity, &ActivityLog::cleared, this, [this] { rebuildConsole(); });
    }
    if (m_ctx.matrix) {
        connect(m_ctx.matrix, &MatrixExecutor::logLine, this, [this](const QString &run, const QString &avd, int, const QString &text) {
            if ((m_loc.kind == Location::Run && run == m_loc.id && avd.isEmpty()) || (m_loc.kind == Location::RunTarget && run == m_loc.id && avd == m_loc.sub)) m_console->appendLine(text);
        });
        connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this] { m_coalesce.start(); });
    }
    if (m_ctx.runtime) connect(m_ctx.runtime, &AndroidRuntime::targetsChanged, this, [this] { m_coalesce.start(); });
    if (m_ctx.scheduler) connect(m_ctx.scheduler, &ResourceScheduler::jobChanged, this, [this] { m_coalesce.start(); update(); });
    setMinimumHeight(bh);
    rebuildConsole();
    refreshVariables();
    setExpanded(false);
}

void DebugArea::setExpanded(bool expanded) {
    m_expanded = expanded;
    for (auto *b : {m_runBtn, m_stopBtn, m_retryBtn, m_shotBtn}) b->setVisible(expanded);
    relayout();
    update();
}

void DebugArea::setStatus(const QString &state, const QString &detail) {
    m_state = state;
    m_detail = detail;
    update();
}

void DebugArea::setLocation(const Location &loc) {
    const bool sameRun = (loc.kind == Location::Run || loc.kind == Location::RunTarget) && loc.id == m_loc.id && loc.kind == m_loc.kind && loc.sub == m_loc.sub;
    m_loc = loc;
    refreshVariables();
    if (!sameRun) rebuildConsole();
    update();
}

QString DebugArea::breadcrumb() const {
    QStringList c{"MobileLab"};
    if (m_loc.kind == Location::Target) c << m_loc.id;
    else if (m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) {
        c << "Run " + m_loc.id.section('-', -2);
        if (m_loc.kind == Location::RunTarget) c << m_loc.sub;
    } else if (m_loc.kind == Location::Container) c << "Waydroid";
    if (m_ctx.scheduler && !m_ctx.scheduler->runningJobs().isEmpty()) c << "Running";
    return c.join("   >   ");
}

void DebugArea::relayout() {
    const int H = height(), W = width(), bh = Metrics::debugBar;
    const bool open = m_expanded && H > bh + 12;
    m_split->setVisible(open);
    m_footer->setVisible(open);
    // bar buttons
    m_toggleBtn->move(6, (bh - m_toggleBtn->height()) / 2);
    int x = 6 + 26 + 8;
    for (auto *b : {m_runBtn, m_stopBtn, m_retryBtn, m_shotBtn}) {
        b->move(x, (bh - b->height()) / 2);
        x += 28;
    }
    m_inspectorBtn->move(W - 6 - m_inspectorBtn->width(), (bh - m_inspectorBtn->height()) / 2);
    if (open) {
        const int fh = Metrics::footerHeight;
        m_split->setGeometry(0, bh, W, H - bh - fh);
        m_footer->setGeometry(0, H - fh, W, fh);
        syncFooter();
    }
    update();
}

void DebugArea::syncFooter() {
    if (!m_split->isVisible()) return;
    const bool both = m_vars->isVisibleTo(this) && m_console->isVisibleTo(this);
    if (both) {
        const auto sizes = m_split->sizes();
        m_footL->setFixedWidth(sizes.value(0) + 1);
        m_footR->setMinimumWidth(0);
        m_footR->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        m_footL->setMinimumWidth(0);
        m_footL->setMaximumWidth(QWIDGETSIZE_MAX);
        m_footR->setMinimumWidth(0);
        m_footR->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void DebugArea::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = width(), H = height(), bh = Metrics::debugBar;
    const bool open = m_split->isVisible();
    // top hairline: separates the editor from the debug area / strip
    p.setPen(t.divider);
    p.drawLine(0, 0, W, 0);
    // debug bar
    p.fillRect(QRect(0, 1, W, bh - 1), t.debugBar);
    p.setPen(Ui::withAlpha(t.text, 22));
    if (open) p.drawLine(0, bh - 1, W, bh - 1);
    // console tint (right pane) with the bottom right corner following the panel's radius
    if (open && m_console->isVisibleTo(this)) {
        const int cx = m_console->mapTo(this, QPoint(0, 0)).x();
        const QRectF body(cx, bh, W - cx, H - bh);
        QPainterPath path;
        const qreal r = Metrics::panelRadius - 1;
        path.moveTo(body.left(), body.top());
        path.lineTo(body.right(), body.top());
        path.lineTo(body.right(), body.bottom() - r);
        path.arcTo(QRectF(body.right() - 2 * r, body.bottom() - 2 * r, 2 * r, 2 * r), 0, -90);
        path.lineTo(body.left(), body.bottom());
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(t.console);
        p.drawPath(path);
        // footer part in the darker tint
        QPainterPath foot;
        const QRectF fr(cx, H - Metrics::footerHeight, W - cx, Metrics::footerHeight);
        foot.moveTo(fr.left(), fr.top());
        foot.lineTo(fr.right(), fr.top());
        foot.lineTo(fr.right(), fr.bottom() - r);
        foot.arcTo(QRectF(fr.right() - 2 * r, fr.bottom() - 2 * r, 2 * r, 2 * r), 0, -90);
        foot.lineTo(fr.left(), fr.bottom());
        foot.closeSubpath();
        p.setBrush(t.consoleFooter);
        p.drawPath(foot);
    }
    // bar text
    p.setFont(Theme::instance().ui(11.5));
    const int left = open ? 6 + 26 + 8 + 4 * 28 + 8 : 6 + 26 + 10;
    const int right = W - 6 - m_inspectorBtn->width() - 10;
    if (open) {
        p.setPen(t.textSecondary);
        const QString bc = breadcrumb();
        const QString rightText = m_ctx.scheduler ? QString("Running %1   |   Queued %2").arg(m_ctx.scheduler->runningJobs().size()).arg(m_ctx.scheduler->queuedJobs().size()) : QString();
        const int rw = QFontMetrics(p.font()).horizontalAdvance(rightText);
        p.drawText(QRect(right - rw, 0, rw, bh), Qt::AlignVCenter | Qt::AlignRight, rightText);
        p.drawText(QRect(left, 0, right - rw - left - 10, bh), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(bc, Qt::ElideRight, right - rw - left - 10));
    } else {
        // collapsed: State | detail, centred like the capsule text
        p.setFont(Theme::instance().ui(11.5, QFont::DemiBold));
        const QFontMetrics fs(p.font());
        const int sw = fs.horizontalAdvance(m_state);
        p.setFont(Theme::instance().ui(11.5));
        const QFontMetrics fr(p.font());
        const QString det = fr.elidedText(m_detail, Qt::ElideRight, qMax(0, right - left - sw - 40));
        const int total = sw + (det.isEmpty() ? 0 : 16 + fr.horizontalAdvance(det));
        int x = left + (right - left - total) / 2;
        p.setFont(Theme::instance().ui(11.5, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(x, 0, sw + 2, bh), Qt::AlignVCenter | Qt::AlignLeft, m_state);
        x += sw;
        if (!det.isEmpty()) {
            p.setFont(Theme::instance().ui(11.5));
            p.setPen(Ui::withAlpha(t.text, 90));
            p.drawText(QRect(x, 0, 16, bh), Qt::AlignCenter, "|");
            p.setPen(t.textSecondary);
            p.drawText(QRect(x + 16, 0, right, bh), Qt::AlignVCenter | Qt::AlignLeft, det);
        }
    }
}

void DebugArea::refreshVariables() {
    const Tokens &t = tk();
    const int mode = m_mode->currentIndex();   // 0 auto, 1 failures, 2 running
    m_vars->rebuild([&](QStandardItemModel &m) {
        if (m_loc.kind == Location::Target && m_ctx.runtime && m_ctx.runtime->target(m_loc.id)) {
            const AndroidTarget &g = *m_ctx.runtime->target(m_loc.id);
            auto *self = var("self", "AndroidTarget", "Target", "v:self", t.accent, "A");
            self->setData(true, NavRole::Expand);
            self->appendRow(var("id", valueText(g.id), "String", "v:id"));
            self->appendRow(var("api", valueText(g.api), "String", "v:api"));
            self->appendRow(var("arch", valueText(g.arch), "String", "v:arch"));
            self->appendRow(var("state", valueText(g.state), "String", "v:state"));
            self->appendRow(var("backend", valueText(g.backend), "String", "v:backend"));
            self->appendRow(var("stability", valueText(g.stability), "String", "v:stability"));
            self->appendRow(var("serial", g.serial.isEmpty() ? "nil" : valueText(g.serial), "String?", "v:serial"));
            self->appendRow(var("pid", QString::number(g.pid), "Int", "v:pid"));
            self->appendRow(var("healthScore", QString::number(g.healthScore), "Int", "v:score"));
            auto *tags = var("tags", QString("%1 values").arg(g.tags.size()), "[String]", "v:tags");
            for (int i = 0; i < g.tags.size(); ++i) tags->appendRow(var(QString("[%1]").arg(i), valueText(g.tags[i]), "String", QString("v:tag%1").arg(i)));
            self->appendRow(tags);
            m.appendRow(self);
        } else if ((m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget) && m_ctx.matrix && m_ctx.matrix->record(m_loc.id)) {
            const MatrixRunRecord &r = *m_ctx.matrix->record(m_loc.id);
            auto badge = [&](RunState s, QString &letter) {
                switch (s) {
                case RunState::Passed: letter = "P"; return t.pass;
                case RunState::Failed: letter = "F"; return t.fail;
                case RunState::Running: letter = "R"; return t.accent;
                case RunState::Skipped: case RunState::Cancelled: letter = "S"; return t.textTertiary;
                default: letter = "-"; return t.textTertiary;
                }
            };
            for (const auto &tg : r.targets) {
                if (m_loc.kind == Location::RunTarget && tg.avd != m_loc.sub) continue;
                if (mode == 1 && tg.state != RunState::Failed) continue;
                if (mode == 2 && tg.state != RunState::Running) continue;
                QString letter;
                const QColor c = badge(tg.state, letter);
                auto *suite = var(tg.avd, QString(), "Suite", "v:s:" + tg.avd, c, letter);
                suite->setData(QString("= %1: %2").arg(runStateName(tg.state), durationText(tg.durationMs)), NavRole::Sub);
                suite->setData(true, NavRole::Expand);
                for (const auto &s : tg.steps) {
                    if (mode == 1 && s.state != RunState::Failed) continue;
                    if (mode == 2 && s.state != RunState::Running) continue;
                    QString sl;
                    const QColor sc = badge(s.state, sl);
                    auto *step = var(s.name, QString(), "Test", "v:c:" + tg.avd + "/" + s.name, sc, sl);
                    step->setData(QString("= %1: %2%3").arg(runStateName(s.state), durationText(s.durationMs), s.message.isEmpty() ? QString() : "  " + s.message), NavRole::Sub);
                    suite->appendRow(step);
                }
                m.appendRow(suite);
            }
        } else {
            // nothing selected: the lab itself
            if (m_ctx.runtime) {
                auto *rt = var("runtime", "AndroidRuntime", "Runtime", "v:rt", t.accent, "L");
                rt->setData(true, NavRole::Expand);
                const QJsonObject o = m_ctx.runtime->status();
                for (auto it = o.begin(); it != o.end(); ++it) rt->appendRow(var(it.key(), valueText(it.value().toVariant()), typeName(it.value().toVariant()), "v:rt:" + it.key()));
                m.appendRow(rt);
            }
            if (m_ctx.scheduler) {
                auto *sc = var("scheduler", "ResourceScheduler", "Scheduler", "v:sch", t.accent, "L");
                sc->setData(true, NavRole::Expand);
                const QJsonObject o = m_ctx.scheduler->status();
                for (auto it = o.begin(); it != o.end(); ++it) sc->appendRow(var(it.key(), valueText(it.value().toVariant()), typeName(it.value().toVariant()), "v:sch:" + it.key()));
                m.appendRow(sc);
            }
        }
    });
}

void DebugArea::rebuildConsole() {
    const bool run = m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget;
    QStringList lines;
    if (run && m_ctx.matrix) {
        lines = m_ctx.matrix->readLog(m_loc.id, m_loc.kind == Location::RunTarget ? m_loc.sub : QString());
    } else if (m_ctx.activity) {
        for (const auto &e : m_ctx.activity->entries()) lines << e.line();
    }
    m_console->setXcodeSyntax(run);
    m_console->setLines(lines);
    applyFilter();
}

void DebugArea::applyFilter() {
    const int idx = m_outputIndex;
    const QString text = m_consoleText.trimmed();
    static const QStringList sources = {"", "", "[runtime]", "[scheduler]", "[matrix]", ""};
    const bool run = m_loc.kind == Location::Run || m_loc.kind == Location::RunTarget;
    if (idx == 0 && text.isEmpty()) { m_console->setFilter({}); return; }
    m_console->setFilter([=](const QString &line) {
        if (!text.isEmpty() && !line.contains(text, Qt::CaseInsensitive)) return false;
        switch (idx) {
        case 1: {
            const auto k = (run ? LogSyntax::classify(line) : LogSyntax::classifyConsole(line)).kind;
            return k == LogSyntax::Kind::Error || k == LogSyntax::Kind::Failure || k == LogSyntax::Kind::CaseFail;
        }
        case 2: case 3: case 4: return line.startsWith(sources[idx]);
        case 5: return line.contains("Test Case") || line.contains("Test Suite") || line.contains("Executed") || line.contains("** TEST");
        default: return true;
        }
    });
}
