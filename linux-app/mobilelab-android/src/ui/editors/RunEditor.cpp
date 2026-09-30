#include "RunEditor.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamWriter>
#include "AndroidRuntime.h"
#include "ArtifactCollector.h"
#include "EditorParts.h"
#include "HostMetrics.h"
#include "Icons.h"
#include "Issues.h"
#include "MatrixExecutor.h"
#include "ResourceScheduler.h"
#include "UiUtil.h"

namespace {
QString durationText(qint64 ms) {
    if (ms <= 0) return "0 s";
    const double s = ms / 1000.0;
    if (s < 60) return QString::number(s, 'f', s < 10 ? 2 : 1) + " s";
    return QString("%1 min %2 s").arg(int(s / 60)).arg(int(s) % 60);
}
QString stateTitle(RunState s) {
    switch (s) {
    case RunState::Passed: return "Tests Passed";
    case RunState::Failed: return "Tests Failed";
    case RunState::Running: return "Running Tests";
    case RunState::Cancelled: return "Cancelled";
    case RunState::Skipped: return "Skipped";
    case RunState::Pending: return "Not Run";
    }
    return {};
}
void stateVisual(RunState s, QString &icon, QColor &color) {
    const Tokens &t = tk();
    switch (s) {
    case RunState::Passed: icon = "checkmark.diamond.fill"; color = t.pass; break;
    case RunState::Failed: icon = "xmark.diamond.fill"; color = t.fail; break;
    case RunState::Running: icon = "arrow.clockwise"; color = t.accent; break;
    case RunState::Cancelled: icon = "diamond"; color = t.warn; break;
    default: icon = "diamond"; color = t.textTertiary; break;
    }
}
QString humanSize(qint64 b) { return HostMetrics::formatBytes(double(b)); }

// Three line failure card: assertion, message, remedy.
class FailureRow : public QAbstractButton {
public:
    FailureRow(const QString &title, const QString &message, const QString &remedy, QWidget *parent) : QAbstractButton(parent), m_title(title), m_msg(message), m_remedy(remedy) {
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(title + ": " + message);
        setAccessibleDescription(remedy);
        setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return QSize(300, heightFor(width() > 0 ? width() : 600)); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return heightFor(w); }
protected:
    void paintEvent(QPaintEvent *) override {
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(underMouse() || isDown() ? Ui::mix(t.lineError, t.fail, 0.06) : t.lineError);
        p.drawRoundedRect(r, 8, 8);
        p.setBrush(t.fail);
        p.drawRoundedRect(QRectF(0, 6, 3, height() - 12), 1.5, 1.5);
        int y = 8;
        Icons::paint(&p, "xmark.diamond.fill", QRectF(12, y + 1, 14, 14), t.fail);
        p.setFont(Theme::instance().ui(13, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(34, y, width() - 44, 18), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        y += 22;
        p.setFont(Theme::instance().mono(12));
        p.setPen(t.text);
        const QRect mr(34, y, width() - 46, 0);
        const QRect used = p.boundingRect(mr.adjusted(0, 0, 0, 2000), Qt::TextWordWrap, m_msg);
        p.drawText(QRect(34, y, width() - 46, used.height()), Qt::TextWordWrap, m_msg);
        y += used.height() + 4;
        if (!m_remedy.isEmpty()) {
            p.setFont(Theme::instance().ui(12));
            p.setPen(t.textSecondary);
            const QRect rr = p.boundingRect(QRect(34, y, width() - 46, 2000), Qt::TextWordWrap, "Remedy: " + m_remedy);
            p.drawText(QRect(34, y, width() - 46, rr.height()), Qt::TextWordWrap, "Remedy: " + m_remedy);
        }
        if (hasFocus()) Ui::drawFocusRing(&p, r, 8);
    }
private:
    int heightFor(int w) const {
        const QFontMetrics mono(Theme::instance().mono(12)), ui(Theme::instance().ui(12));
        const int tw = qMax(120, w - 46);
        int h = 8 + 22 + mono.boundingRect(QRect(0, 0, tw, 2000), Qt::TextWordWrap, m_msg).height() + 4;
        if (!m_remedy.isEmpty()) h += ui.boundingRect(QRect(0, 0, tw, 2000), Qt::TextWordWrap, "Remedy: " + m_remedy).height() + 4;
        return h + 6;
    }
    QString m_title, m_msg, m_remedy;
};

QWidget *sectionHeader(const QString &text, QWidget *parent) {
    auto *l = new ThemedLabel(text.toUpper(), 11, QFont::Bold, ThemedLabel::Role::Secondary, parent);
    l->setContentsMargins(2, 14, 0, 2);
    return l;
}
}  // namespace

// ---- JUnit export ------------------------------------------------------------------------------------

bool RunEditor::exportJUnit(const MatrixRunRecord &r, QString *pathOut) {
    const QString path = r.dir + "/junit.xml";
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QXmlStreamWriter x(&f);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeStartElement("testsuites");
    x.writeAttribute("name", "Matrix " + r.id);
    x.writeAttribute("tests", QString::number(r.totalSteps()));
    x.writeAttribute("failures", QString::number(r.failedSteps()));
    x.writeAttribute("time", QString::number(r.durationMs() / 1000.0, 'f', 3));
    for (const auto &t : r.targets) {
        int skipped = 0;
        for (const auto &s : t.steps) if (s.state == RunState::Skipped || s.state == RunState::Cancelled || s.state == RunState::Pending) ++skipped;
        x.writeStartElement("testsuite");
        x.writeAttribute("name", t.avd);
        x.writeAttribute("tests", QString::number(t.steps.size()));
        x.writeAttribute("failures", QString::number(t.failedSteps()));
        x.writeAttribute("skipped", QString::number(skipped));
        x.writeAttribute("time", QString::number(t.durationMs / 1000.0, 'f', 3));
        if (t.started.isValid()) x.writeAttribute("timestamp", t.started.toUTC().toString(Qt::ISODate));
        x.writeStartElement("properties");
        for (const auto &kv : {QPair<QString, QString>{"abi", t.abi}, {"api", t.api}, {"serial", t.serial}}) {
            x.writeEmptyElement("property");
            x.writeAttribute("name", kv.first);
            x.writeAttribute("value", kv.second);
        }
        x.writeEndElement();
        for (const auto &s : t.steps) {
            x.writeStartElement("testcase");
            x.writeAttribute("classname", t.avd);
            x.writeAttribute("name", s.name);
            x.writeAttribute("time", QString::number(s.durationMs / 1000.0, 'f', 3));
            if (s.state == RunState::Failed) {
                x.writeStartElement("failure");
                x.writeAttribute("message", s.message);
                x.writeCharacters(s.message + "\n" + remedyForFailure(s.name, s.message));
                x.writeEndElement();
            } else if (s.state != RunState::Passed) {
                x.writeEmptyElement("skipped");
                x.writeAttribute("message", s.message.isEmpty() ? runStateName(s.state) : s.message);
            }
            x.writeEndElement();
        }
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndDocument();
    if (!f.commit()) return false;
    if (pathOut) *pathOut = path;
    return true;
}

// ---- RunEditor ---------------------------------------------------------------------------------------

RunEditor::RunEditor(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *top = new QWidget(this);
    auto *th = new QHBoxLayout(top);
    th->setContentsMargins(0, 6, 0, 4);
    m_tabs = new SegmentedControl({"Summary", "Tests", "Logs"}, top);
    m_tabs->setAccessibleName("Report view");
    th->addStretch();
    th->addWidget(m_tabs);
    th->addStretch();
    v->addWidget(top);
    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack, 1);
    m_summaryScroll = new QScrollArea(this);
    m_summaryScroll->setWidgetResizable(true);
    m_summaryScroll->setFrameShape(QFrame::NoFrame);
    m_summaryScroll->viewport()->setAutoFillBackground(false);
    m_summaryScroll->setAccessibleName("Report summary");
    m_stack->addWidget(m_summaryScroll);
    m_tests = new NavTree(this);
    m_tests->setEmpty("diamond", "No tests", "This run has no test cases yet.");
    m_tests->setAccessibleName("Report tests");
    m_stack->addWidget(m_tests);
    auto *logPage = new QWidget(this);
    auto *lv = new QVBoxLayout(logPage);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);
    auto *bar = new QWidget(logPage);
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(14, 2, 14, 4);
    auto *lbl = new ThemedLabel("Source", 11, QFont::DemiBold, ThemedLabel::Role::Secondary, bar);
    bh->addWidget(lbl);
    m_source = new PopupButton(bar);
    m_source->setFlat(true);
    m_source->setAccessibleName("Log source");
    bh->addWidget(m_source);
    bh->addStretch();
    m_logInfo = new ThemedLabel({}, 11, QFont::Normal, ThemedLabel::Role::Tertiary, bar);
    bh->addWidget(m_logInfo);
    lv->addWidget(bar);
    m_log = new LogView(LogView::Mode::Source, logPage);
    m_log->setEmptyText("No log output yet.");
    lv->addWidget(m_log, 1);
    m_stack->addWidget(logPage);
    connect(m_tabs, &SegmentedControl::currentChanged, this, [this](int i) { setTab(i, true); });
    connect(m_source, &PopupButton::currentChanged, this, [this](int i) { m_logSource = i; reloadLog(false); });
    m_coalesce.setParent(this);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(120);
    connect(&m_coalesce, &QTimer::timeout, this, [this] { rebuildSummary(); rebuildTests(); });
    connect(m_tests, &NavTree::locationRequested, this, &RunEditor::locationRequested);
    connect(m_ctx.matrix, &MatrixExecutor::logLine, this, [this](const QString &run, const QString &avd, int, const QString &text) {
        if (run == m_runId && avd == m_avd && m_logSource == 0) m_log->appendLine(text);
    });
    connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this](const QString &id) {
        if (id.isEmpty() || id == m_runId) m_coalesce.start();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] { rebuildSummary(); rebuildTests(); });
}

QString RunEditor::tab() const { return QStringList{"summary", "tests", "logs"}.value(m_tabs->current()); }

void RunEditor::setTab(int i, bool emitLocation) {
    m_stack->setCurrentIndex(i);
    if (m_tabs->current() != i) m_tabs->setCurrent(i);
    if (i == 2) reloadLog(false);
    if (emitLocation) {
        Location l;
        l.kind = m_avd.isEmpty() ? Location::Run : Location::RunTarget;
        l.id = m_runId;
        l.sub = m_avd;
        l.tab = tab();
        emit locationRequested(l);
    }
}

void RunEditor::showLocation(const Location &loc) {
    const bool sameSubject = loc.id == m_runId && loc.sub == m_avd;
    m_runId = loc.id;
    m_avd = loc.kind == Location::RunTarget ? loc.sub : QString();
    const int tabIndex = loc.tab == "tests" ? 1 : loc.tab == "logs" ? 2 : 0;
    if (!sameSubject) { m_logSource = 0; }
    rebuildSummary();
    rebuildTests();
    m_source->blockSignals(true);
    m_source->setItems(logSources(), m_logSource);
    m_source->blockSignals(false);
    m_tabs->setCurrent(tabIndex);
    m_stack->setCurrentIndex(tabIndex);
    if (tabIndex == 2 || loc.line >= 0) {
        reloadLog(false);
        if (loc.line >= 0 && tabIndex == 2) m_log->scrollToLine(loc.line);
    } else {
        m_log->clear();
    }
    setAccessibleName(m_avd.isEmpty() ? "Run report " + m_runId : "Target report " + m_avd);
}

void RunEditor::refresh() {
    m_coalesce.start();
}

QStringList RunEditor::logSources() const {
    QStringList s{m_avd.isEmpty() ? "Run Log" : "Test Log"};
    const auto *r = m_ctx.matrix->record(m_runId);
    if (r && !m_avd.isEmpty())
        for (const auto &t : r->targets)
            if (t.avd == m_avd) {
                if (QFileInfo::exists(t.artifactDir + "/logcat.txt")) s << "Logcat";
                if (QFileInfo::exists(t.artifactDir + "/emulator.log")) s << "Emulator Output";
            }
    return s;
}

QString RunEditor::sourcePath(int index) const {
    const auto *r = m_ctx.matrix->record(m_runId);
    if (!r) return {};
    if (index == 0) return m_ctx.matrix->logPathFor(m_runId, m_avd);
    const QStringList s = logSources();
    for (const auto &t : r->targets)
        if (t.avd == m_avd) return t.artifactDir + (s.value(index) == "Logcat" ? "/logcat.txt" : "/emulator.log");
    return {};
}

void RunEditor::reloadLog(bool keepPosition) {
    Q_UNUSED(keepPosition);
    m_source->blockSignals(true);
    m_source->setItems(logSources(), m_logSource);
    m_source->blockSignals(false);
    const QString path = sourcePath(m_logSource);
    QStringList lines;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        lines = QString::fromUtf8(f.readAll()).split('\n');
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    }
    const bool live = m_ctx.matrix->currentRunId() == m_runId;
    m_log->setFollow(live);
    m_log->setLines(lines);
    m_log->setEmptyText(QFileInfo::exists(path) ? "This log is empty." : "No log file for this selection.");
    m_logInfo->setText(path.isEmpty() ? QString() : QString("%1 lines  %2").arg(lines.size()).arg(QFileInfo(path).fileName()));
    m_log->setToolTip(path);
    if (live && !lines.isEmpty()) m_log->setCurrentLine(lines.size() - 1, false);
}

void RunEditor::rebuildTests() {
    const auto *r = m_ctx.matrix->record(m_runId);
    const Tokens &t = tk();
    m_tests->rebuild([&](QStandardItemModel &m) {
        if (!r) return;
        auto add = [&](QStandardItem *parent, const QString &text, const QString &id, RunState st, const QString &trailing, const QString &sub, const Location &loc) {
            QString icon; QColor col;
            stateVisual(st, icon, col);
            auto *it = new QStandardItem(text);
            it->setEditable(false);
            it->setData(id, NavRole::Id);
            if (st == RunState::Running) it->setData("spin", NavRole::Status);
            else { it->setData(icon, NavRole::Icon); it->setData(col, NavRole::IconColor); }
            it->setData(trailing, NavRole::Trailing);
            it->setData(sub, NavRole::Sub);
            it->setData(QVariant::fromValue(loc), NavRole::Loc);
            it->setData(true, NavRole::Expand);
            if (parent) parent->appendRow(it); else m.appendRow(it);
            return it;
        };
        for (const auto &tg : r->targets) {
            if (!m_avd.isEmpty() && tg.avd != m_avd) continue;
            Location sl; sl.kind = Location::RunTarget; sl.id = r->id; sl.sub = tg.avd; sl.tab = "tests";
            auto *suite = add(nullptr, tg.avd, "s:" + tg.avd, tg.state, durationText(tg.durationMs), QString("%1, API %2").arg(tg.abi, tg.api), sl);
            for (const auto &s : tg.steps) {
                Location cl = sl; cl.tab = "logs"; cl.line = s.state == RunState::Failed && s.failLine >= 0 ? s.failLine : s.logLine;
                add(suite, s.name, "c:" + tg.avd + "/" + s.name, s.state, s.state == RunState::Pending ? QString() : durationText(s.durationMs), s.message, cl);
            }
        }
    });
    Q_UNUSED(t);
}

void RunEditor::rebuildSummary() {
    const auto *r = m_ctx.matrix->record(m_runId);
    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(28, 12, 28, 28);
    root->setSpacing(6);
    if (!r) {
        root->addWidget(new ThemedLabel("This run no longer exists.", 13, QFont::Normal, ThemedLabel::Role::Secondary, content));
        root->addStretch();
        m_summaryScroll->setWidget(content);
        return;
    }
    const TargetResult *tg = nullptr;
    if (!m_avd.isEmpty())
        for (const auto &t : r->targets)
            if (t.avd == m_avd) tg = &t;
    const RunState state = tg ? tg->state : r->state;
    QString icon;
    QColor col;
    stateVisual(state, icon, col);
    // header
    auto *head = new QWidget(content);
    auto *hh = new QHBoxLayout(head);
    hh->setContentsMargins(0, 4, 0, 0);
    hh->setSpacing(12);
    class IconLabel : public QWidget {
    public:
        IconLabel(const QString &i, const QColor &c, QWidget *p) : QWidget(p), m_i(i), m_c(c) { setFixedSize(30, 30); }
    protected:
        void paintEvent(QPaintEvent *) override { QPainter p(this); p.setRenderHint(QPainter::Antialiasing); Icons::paint(&p, m_i, QRectF(2, 2, 26, 26), m_c); }
    private:
        QString m_i; QColor m_c;
    };
    hh->addWidget(new IconLabel(icon, col, head), 0, Qt::AlignTop);
    auto *tv = new QVBoxLayout;
    tv->setSpacing(2);
    tv->addWidget(new ThemedLabel(tg ? tg->avd + ": " + stateTitle(state) : stateTitle(state), 22, QFont::DemiBold, ThemedLabel::Role::Text, head));
    QStringList sub;
    sub << "Android Matrix";
    if (tg) sub << QString("%1, API %2").arg(tg->abi, tg->api);
    else sub << QString("%1 %2").arg(r->targets.size()).arg(r->targets.size() == 1 ? "target" : "targets");
    sub << "Started " + r->started.toLocalTime().toString("ddd d MMM, HH:mm:ss");
    sub << "Duration " + durationText(tg ? tg->durationMs : r->durationMs());
    tv->addWidget(new ThemedLabel(sub.join("   |   "), 12, QFont::Normal, ThemedLabel::Role::Secondary, head));
    tv->addWidget(new ThemedLabel(r->id, 11, QFont::Normal, ThemedLabel::Role::Tertiary, head, true));
    hh->addLayout(tv, 1);
    root->addWidget(head);
    // counters
    int passed = 0, failed = 0, skipped = 0, pending = 0;
    auto count = [&](const TargetResult &t) {
        for (const auto &s : t.steps) {
            if (s.state == RunState::Passed) ++passed;
            else if (s.state == RunState::Failed) ++failed;
            else if (s.state == RunState::Skipped || s.state == RunState::Cancelled) ++skipped;
            else ++pending;
        }
    };
    if (tg) count(*tg); else for (const auto &t : r->targets) count(t);
    auto *counters = new QWidget(content);
    auto *ch = new QHBoxLayout(counters);
    ch->setContentsMargins(0, 14, 0, 4);
    ch->setSpacing(36);
    auto counter = [&](int n, const QString &label, ThemedLabel::Role role) {
        auto *w = new QWidget(counters);
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(new ThemedLabel(QString::number(n), 26, QFont::DemiBold, role, w));
        l->addWidget(new ThemedLabel(label, 11, QFont::Medium, ThemedLabel::Role::Secondary, w));
        ch->addWidget(w);
    };
    counter(passed, "PASSED", ThemedLabel::Role::Pass);
    counter(failed, "FAILED", failed ? ThemedLabel::Role::Fail : ThemedLabel::Role::Secondary);
    counter(skipped, "SKIPPED", ThemedLabel::Role::Secondary);
    if (pending) counter(pending, "PENDING", ThemedLabel::Role::Secondary);
    ch->addStretch();
    root->addWidget(counters);
    auto *bar = new StatBar(content);
    bar->setCounts(passed, failed, skipped + pending);
    root->addWidget(bar);
    // failures
    QVector<const StepResult *> fails;
    QVector<const TargetResult *> failTargets;
    auto collect = [&](const TargetResult &t) {
        for (const auto &s : t.steps)
            if (s.state == RunState::Failed) { fails << &s; failTargets << &t; }
    };
    if (tg) collect(*tg); else for (const auto &t : r->targets) collect(t);
    if (!fails.isEmpty()) {
        root->addWidget(sectionHeader("Failures", content));
        for (int i = 0; i < fails.size(); ++i) {
            const auto *s = fails[i];
            const auto *t = failTargets[i];
            auto *row = new FailureRow(QString("%1 / %2").arg(t->avd, s->name), s->message, remedyForFailure(s->name, s->message), content);
            Location l; l.kind = Location::RunTarget; l.id = r->id; l.sub = t->avd; l.tab = "logs"; l.line = s->failLine >= 0 ? s->failLine : s->logLine;
            connect(row, &QAbstractButton::clicked, this, [this, l] { emit locationRequested(l); });
            root->addWidget(row);
        }
    }
    if (!r->error.isEmpty()) {
        root->addWidget(sectionHeader("Notes", content));
        root->addWidget(new ThemedLabel(r->error, 12, QFont::Normal, ThemedLabel::Role::Warn, content));
    }
    // targets / steps
    if (!tg) {
        root->addWidget(sectionHeader("Targets", content));
        for (const auto &t : r->targets) {
            QString ic; QColor c;
            stateVisual(t.state, ic, c);
            const int f = t.failedSteps(), n = t.executedSteps();
            const QString trailing = QString("%1  %2").arg(t.state == RunState::Pending ? "Waiting" : QString("%1 of %2 failed").arg(f).arg(n), t.durationMs ? durationText(t.durationMs) : QString());
            auto *row = new RowButton(ic, t.avd, QString("%1, API %2").arg(t.abi, t.api), trailing.trimmed(), content);
            row->setIconColor(c);
            Location l; l.kind = Location::RunTarget; l.id = r->id; l.sub = t.avd; l.tab = "summary";
            connect(row, &QAbstractButton::clicked, this, [this, l] { emit locationRequested(l); });
            root->addWidget(row);
        }
    } else {
        root->addWidget(sectionHeader("Test Cases", content));
        for (const auto &s : tg->steps) {
            QString ic; QColor c;
            stateVisual(s.state, ic, c);
            auto *row = new RowButton(ic, s.name, s.message, s.state == RunState::Pending ? QString() : durationText(s.durationMs), content);
            row->setIconColor(c);
            Location l; l.kind = Location::RunTarget; l.id = r->id; l.sub = tg->avd; l.tab = "logs"; l.line = s.state == RunState::Failed && s.failLine >= 0 ? s.failLine : s.logLine;
            connect(row, &QAbstractButton::clicked, this, [this, l] { emit locationRequested(l); });
            root->addWidget(row);
        }
    }
    // artifacts
    root->addWidget(sectionHeader("Artifacts", content));
    QDir dir(tg ? tg->artifactDir : r->dir);
    const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
    if (files.isEmpty()) root->addWidget(new ThemedLabel("No files were collected yet.", 12, QFont::Normal, ThemedLabel::Role::Tertiary, content));
    for (const auto &fi : files) {
        const QString name = fi.fileName();
        const QString ico = name.endsWith(".png") ? "camera" : name.endsWith(".json") || name.endsWith(".xml") ? "doc.text" : "doc.text";
        auto *row = new RowButton(ico, name, fi.absoluteFilePath(), humanSize(fi.size()), content);
        row->setToolTip("Open " + fi.absoluteFilePath());
        const QString path = fi.absoluteFilePath();
        connect(row, &QAbstractButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        root->addWidget(row);
    }
    if (!tg) {
        for (const auto &t : r->targets) {
            QDir d(t.artifactDir);
            for (const auto &fi : d.entryInfoList(QDir::Files, QDir::Name)) {
                if (fi.fileName() == "test.log") continue;
                auto *row = new RowButton(fi.suffix() == "png" ? "camera" : "doc.text", t.avd + "/" + fi.fileName(), {}, humanSize(fi.size()), content);
                const QString path = fi.absoluteFilePath();
                connect(row, &QAbstractButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
                root->addWidget(row);
            }
        }
    }
    // actions
    auto *actions = new QWidget(content);
    auto *ah = new QHBoxLayout(actions);
    ah->setContentsMargins(0, 16, 0, 0);
    ah->setSpacing(8);
    const bool running = m_ctx.matrix->currentRunId() == r->id;
    auto *again = new PillButton("Run Again", PillButton::Style::Primary, actions, "play.fill");
    again->setEnabled(!m_ctx.matrix->isRunning());
    connect(again, &QAbstractButton::clicked, this, [this, r] { emit action("run-again", r->id); });
    ah->addWidget(again);
    if (running) {
        auto *cancel = new PillButton("Cancel", PillButton::Style::Secondary, actions, "stop.fill");
        connect(cancel, &QAbstractButton::clicked, this, [this] { emit action("stop", {}); });
        ah->addWidget(cancel);
    }
    auto *junit = new PillButton("Export JUnit", PillButton::Style::Secondary, actions, "square.and.arrow.down");
    junit->setEnabled(!running);
    connect(junit, &QAbstractButton::clicked, this, [this, r] { emit action("export-junit", r->id); });
    ah->addWidget(junit);
    auto *copy = new PillButton("Copy Artifact Path", PillButton::Style::Plain, actions, "doc.on.doc");
    const QString dirPath = tg ? tg->artifactDir : r->dir;
    connect(copy, &QAbstractButton::clicked, this, [dirPath] { QApplication::clipboard()->setText(dirPath); });
    ah->addWidget(copy);
    ah->addStretch();
    root->addWidget(actions);
    root->addStretch();
    const int scroll = m_summaryScroll->verticalScrollBar()->value();
    m_summaryScroll->setWidget(content);
    m_summaryScroll->verticalScrollBar()->setValue(scroll);
}
