#include "OpenQuickly.h"
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QVBoxLayout>
#include "AndroidRuntime.h"
#include "GlassMenu.h"
#include "Icons.h"
#include "MatrixExecutor.h"
#include "NavCommon.h"
#include "UiUtil.h"

QVector<OpenQuickly::Item> OpenQuickly::collect(const AppContext &ctx) {
    const Tokens &t = tk();
    QVector<Item> out;
    if (ctx.runtime)
        for (const auto &g : ctx.runtime->targets()) {
            Item i;
            i.title = g.id;
            i.sub = QString("API %1, %2, %3").arg(g.api, g.arch, g.state);
            i.icon = g.device.contains("tablet", Qt::CaseInsensitive) ? "ipad" : "iphone";
            i.kind = "Device";
            i.loc.kind = Location::Target;
            i.loc.id = g.id;
            out << i;
        }
    if (ctx.matrix) {
        const auto &recs = ctx.matrix->records();
        for (int n = recs.size() - 1; n >= 0 && out.size() < 600; --n) {
            const auto &r = recs[n];
            Item i;
            i.title = "Matrix " + r.started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
            i.sub = QString("%1, %2 targets").arg(runStateName(r.state)).arg(r.targets.size());
            i.icon = r.state == RunState::Passed ? "checkmark.diamond.fill" : r.state == RunState::Failed ? "xmark.diamond.fill" : "diamond";
            i.color = r.state == RunState::Passed ? t.pass : r.state == RunState::Failed ? t.fail : t.textSecondary;
            i.kind = "Run";
            i.loc.kind = Location::Run;
            i.loc.id = r.id;
            i.loc.tab = "summary";
            out << i;
            for (const auto &tg : r.targets) {
                Item ti;
                ti.title = tg.avd + "  in  " + i.title;
                ti.sub = runStateName(tg.state);
                ti.icon = tg.state == RunState::Passed ? "checkmark.diamond.fill" : tg.state == RunState::Failed ? "xmark.diamond.fill" : "diamond";
                ti.color = tg.state == RunState::Passed ? t.pass : tg.state == RunState::Failed ? t.fail : t.textSecondary;
                ti.kind = "Report";
                ti.loc.kind = Location::RunTarget;
                ti.loc.id = r.id;
                ti.loc.sub = tg.avd;
                ti.loc.tab = "summary";
                out << ti;
                for (const auto &s : tg.steps) {
                    if (s.state != RunState::Failed) continue;
                    Item si = ti;
                    si.title = tg.avd + " / " + s.name;
                    si.sub = s.message;
                    si.kind = "Test";
                    si.loc.tab = "logs";
                    si.loc.line = s.failLine >= 0 ? s.failLine : s.logLine;
                    out << si;
                }
            }
        }
    }
    struct Cmd { const char *title, *command, *icon; };
    static const Cmd cmds[] = {{"Run Matrix", "run", "play.fill"}, {"Stop", "stop", "stop.fill"}, {"New Virtual Device", "new-avd", "plus"},
                               {"Settings", "settings", "gearshape"}, {"Diagnostics", "diagnostics", "exclamationmark.triangle"},
                               {"Open in VS Code", "vscode", "code"}, {"Refresh Targets", "refresh", "arrow.clockwise"},
                               {"Keyboard Shortcuts", "shortcuts", "info.circle"}};
    for (const auto &c : cmds) {
        Item i;
        i.title = c.title;
        i.icon = c.icon;
        i.kind = "Command";
        i.command = c.command;
        out << i;
    }
    return out;
}

QVector<OpenQuickly::Item> OpenQuickly::rank(const QVector<Item> &all, const QString &query, int limit) {
    QVector<QPair<int, int>> scored;
    for (int i = 0; i < all.size(); ++i) {
        const int s = Ui::fuzzyScore(query, all[i].title);
        const int s2 = query.isEmpty() ? 0 : Ui::fuzzyScore(query, all[i].sub) - 30;
        const int best = qMax(s, s2 < 0 ? -1 : s2);
        if (best < 0 && !query.isEmpty()) continue;
        scored.push_back({query.isEmpty() ? -i : best, i});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QVector<Item> out;
    for (const auto &p : scored) {
        if (out.size() >= limit) break;
        out << all[p.second];
    }
    return out;
}

OpenQuickly::OpenQuickly(const AppContext &ctx, QWidget *parent) : QDialog(parent), m_ctx(ctx) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowModality(Qt::WindowModal);
    setAccessibleName("Open Quickly");
    setFixedSize(600 + 40, 380 + 40);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20 + 12, 20 + 12, 20 + 12, 20 + 8);
    v->setSpacing(6);
    m_edit = new QLineEdit(this);
    m_edit->setProperty("bare", true);
    m_edit->setFont(Theme::instance().ui(17));
    m_edit->setPlaceholderText("Open Quickly");
    m_edit->setAccessibleName("Open Quickly search");
    m_edit->setFrame(false);
    m_edit->setFixedHeight(34);
    m_edit->setTextMargins(24, 0, 0, 0);
    m_edit->installEventFilter(this);
    v->addWidget(m_edit);
    m_list = new NavTree(this);
    m_list->setRootIsDecorated(false);
    m_list->setIndentation(0);
    m_list->setAccessibleName("Results");
    m_list->setEmpty("magnifyingglass", "No matches", "Nothing matches this search.");
    v->addWidget(m_list, 1);
    connect(m_edit, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(m_edit, &QLineEdit::returnPressed, this, &OpenQuickly::choose);
    connect(m_list, &NavTree::activated, this, [this] { choose(); });
    connect(m_list, &NavTree::clicked, this, [this] { choose(); });
    m_all = collect(ctx);
    refill();
}

void OpenQuickly::setQuery(const QString &q) { m_edit->setText(q); }

void OpenQuickly::showEvent(QShowEvent *e) {
    if (QWidget *p = parentWidget() ? parentWidget()->window() : nullptr) {
        const QRect pr = p->frameGeometry();
        move(pr.center().x() - width() / 2, pr.top() + 70 - 20);
    }
    m_materialSize = QSize();
    QDialog::showEvent(e);
    m_edit->setFocus();
}

void OpenQuickly::refill() {
    m_results = rank(m_all, m_edit->text().trimmed());
    const Tokens &t = tk();
    m_list->rebuild([&](QStandardItemModel &m) {
        for (int i = 0; i < m_results.size(); ++i) {
            const Item &it = m_results[i];
            auto *row = new QStandardItem(it.title);
            row->setEditable(false);
            row->setData(QString::number(i), NavRole::Id);
            row->setData(it.icon, NavRole::Icon);
            row->setData(it.color.isValid() ? it.color : (it.kind == "Command" ? t.accent : t.textSecondary), NavRole::IconColor);
            row->setData(it.sub, NavRole::Sub);
            row->setData(it.kind, NavRole::Trailing);
            row->setData(QVariant::fromValue(it.loc), NavRole::Loc);
            m.appendRow(row);
        }
    });
    if (m_list->source()->rowCount() > 0) m_list->selectId("0");
}

bool OpenQuickly::eventFilter(QObject *o, QEvent *e) {
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int cur = m_list->currentId().toInt();
        const int n = m_results.size();
        if (ke->key() == Qt::Key_Down && n) { m_list->selectId(QString::number(qMin(cur + 1, n - 1))); return true; }
        if (ke->key() == Qt::Key_Up && n) { m_list->selectId(QString::number(qMax(cur - 1, 0))); return true; }
        if (ke->key() == Qt::Key_Escape) { reject(); return true; }
    }
    return QDialog::eventFilter(o, e);
}

void OpenQuickly::choose() {
    const int idx = m_list->currentId().toInt();
    if (idx < 0 || idx >= m_results.size()) return;
    const Item it = m_results[idx];
    accept();
    if (!it.command.isEmpty()) emit commandChosen(it.command);
    else emit locationChosen(it.loc);
}

QImage OpenQuickly::grabPopup() {
    QImage img(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(devicePixelRatioF());
    img.fill(Qt::transparent);
    render(&img);
    return img;
}

void OpenQuickly::paintEvent(QPaintEvent *) {
    const QRect sheet = rect().adjusted(20, 20, -20, -20);
    if (m_materialSize != size()) {
        QWidget *under = parentWidget() ? parentWidget()->window() : nullptr;
        m_material = renderPopupMaterial(under, QRect(mapToGlobal(sheet.topLeft()), sheet.size()), devicePixelRatioF(), 16);
        m_materialSize = size();
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Ui::drawSoftShadow(&p, sheet, 16, 10, 6, tk().dark ? QColor(0, 0, 0, 170) : QColor(20, 30, 60, 90));
    p.drawImage(sheet.topLeft(), m_material);
    // search icon and separator under the field
    Icons::paint(&p, "magnifyingglass", QRectF(sheet.left() + 12, sheet.top() + 16, 16, 16), tk().textSecondary);
    p.setPen(Ui::withAlpha(tk().text, 26));
    p.drawLine(sheet.left() + 10, sheet.top() + 50, sheet.right() - 10, sheet.top() + 50);
}
