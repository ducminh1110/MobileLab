#include "JumpBar.h"
#include <QAction>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include "GlassMenu.h"
#include "Icons.h"
#include "UiUtil.h"

JumpBar::JumpBar(QAction *back, QAction *forward, QAction *related, QAction *options, QAction *add, QWidget *parent) : QWidget(parent) {
    setFixedHeight(Metrics::jumpBarHeight + 6);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName("Jump bar");
    m_grid = new IconButton("square.grid.2x2", this);
    m_grid->setFixedButtonSize(24, 22);
    m_grid->setGlyphSize(14);
    m_grid->setTint(tk().textTertiary);
    Ui::setTip(m_grid, "Recent Locations");
    connect(m_grid, &QAbstractButton::clicked, this, [this] { emit gridRequested(m_grid->mapToGlobal(QPoint(0, m_grid->height() + 4))); });
    m_nav = new GlassGroup(this, Glass::Kind::Quiet);
    m_nav->setObjectName("jump-nav");
    m_nav->setButtonSize(24, 22);
    m_nav->addButton(back, "chevron.left");
    m_nav->addButton(forward, "chevron.right");
    m_trail = new GlassGroup(this, Glass::Kind::Quiet);
    m_trail->setObjectName("jump-trailing");
    m_trail->setButtonSize(26, 22);
    m_trail->addButton(related, "arrow.left.arrow.right");
    m_trail->addButton(options, "list.bullet.indent");
    m_trail->addButton(add, "plus");
    for (auto *b : m_nav->buttons()) { b->setGlyphSize(12); }
    for (auto *b : m_trail->buttons()) { b->setGlyphSize(13); b->setTint(tk().textSecondary); }
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_grid->setTint(tk().textTertiary);
        for (auto *b : m_trail->buttons()) b->setTint(tk().textSecondary);
        update();
    });
}

void JumpBar::setCrumbs(const QVector<Crumb> &c) {
    m_crumbs = c;
    m_hover = m_pressed = -1;
    update();
}

void JumpBar::relayout() {
    const int H = height();
    m_grid->move(8, (H - m_grid->height()) / 2);
    const QSize ns = m_nav->sizeHint();
    m_nav->setGeometry(8 + 24 + 4, (H - ns.height()) / 2, ns.width(), ns.height());
    m_left = m_nav->geometry().right() + 10;
    const QSize ts = m_trail->sizeHint();
    m_trail->setGeometry(width() - ts.width() - 8, (H - ts.height()) / 2, ts.width(), ts.height());
    m_right = m_trail->geometry().left() - 8;
    update();
}

QVector<JumpBar::Slot> JumpBar::layoutCrumbs() const {
    QVector<Slot> rects;
    const QFont f = Theme::instance().ui(11.5);
    const QFontMetrics fm(f);
    const int avail = qMax(0, m_right - m_left);
    // natural widths: icon 14 + gap 5 + text + padding 8, separator 14
    QVector<int> w;
    int total = 0;
    for (const auto &c : m_crumbs) {
        w << 8 + (c.icon.isEmpty() ? 0 : 19) + fm.horizontalAdvance(c.text) + 4;
        total += w.last() + 14;
    }
    total -= 14;
    // shrink the widest crumbs first, keep the last visible
    while (total > avail) {
        int widest = -1;
        for (int i = 0; i < w.size(); ++i)
            if (w[i] > 60 && (widest < 0 || w[i] > w[widest])) widest = i;
        if (widest < 0) break;
        const int cut = qMin(w[widest] - 60, total - avail);
        w[widest] -= cut;
        total -= cut;
    }
    int x = m_left;
    for (int i = 0; i < m_crumbs.size(); ++i) {
        rects.push_back({QRect(x, (height() - 22) / 2, w[i], 22), i});
        x += w[i] + 14;
    }
    return rects;
}

int JumpBar::crumbAt(const QPoint &p) const {
    for (const auto &s : layoutCrumbs())
        if (s.rect.contains(p)) return s.crumb;
    return -1;
}

void JumpBar::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont f = Theme::instance().ui(11.5);
    p.setFont(f);
    const auto rects = layoutCrumbs();
    for (int k = 0; k < rects.size(); ++k) {
        const auto &s = rects[k];
        const Crumb &c = m_crumbs[s.crumb];
        const bool last = s.crumb == m_crumbs.size() - 1;
        if (m_hover == s.crumb || m_pressed == s.crumb) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pressed == s.crumb ? t.glassPressed : t.glassHover);
            p.drawRoundedRect(QRectF(s.rect), 6, 6);
        }
        int x = s.rect.left() + 6;
        if (!c.icon.isEmpty()) {
            Icons::paint(&p, c.icon, QRectF(x, s.rect.center().y() - 7, 14, 14), c.iconColor.isValid() ? c.iconColor : t.folder);
            x += 19;
        }
        p.setPen(last ? t.text : t.textSecondary);
        p.drawText(QRect(x, s.rect.top(), s.rect.right() - x - 2, s.rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(c.text, Qt::ElideRight, s.rect.right() - x - 2));
        if (k + 1 < rects.size()) Icons::paint(&p, "chevron.right", QRectF(s.rect.right() + 3, s.rect.center().y() - 4, 8, 8), t.textTertiary);
        if (m_kbFocus && hasFocus() && m_focus == s.crumb) Ui::drawFocusRing(&p, QRectF(s.rect), 6);
    }
}

void JumpBar::mouseMoveEvent(QMouseEvent *e) {
    const int i = crumbAt(e->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void JumpBar::mousePressEvent(QMouseEvent *e) {
    m_pressed = crumbAt(e->position().toPoint());
    update();
}

void JumpBar::mouseReleaseEvent(QMouseEvent *e) {
    const int i = crumbAt(e->position().toPoint());
    const int pressed = m_pressed;
    m_pressed = -1;
    update();
    if (i >= 0 && i == pressed) openCrumb(i);
}

void JumpBar::openCrumb(int i) {
    const Crumb &c = m_crumbs[i];
    for (const auto &s : layoutCrumbs()) {
        if (s.crumb != i) continue;
        if (c.siblings.isEmpty()) {
            emit locationRequested(c.loc);
            return;
        }
        GlassMenu menu(this);
        for (const auto &sib : c.siblings) {
            QAction *a = menu.addAction(Icons::icon(sib.icon.isEmpty() ? "circle" : sib.icon, sib.icon == "folder.fill" ? tk().folder : tk().textSecondary, 16), sib.text);
            a->setCheckable(true);
            a->setChecked(sib.current);
            const Location l = sib.loc;
            connect(a, &QAction::triggered, this, [this, l] { emit locationRequested(l); });
        }
        menu.exec(mapToGlobal(QPoint(s.rect.left(), s.rect.bottom() + 4)));
        return;
    }
}

void JumpBar::focusInEvent(QFocusEvent *e) {
    m_kbFocus = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
    if (m_focus < 0 || m_focus >= m_crumbs.size()) m_focus = qMax(0, int(m_crumbs.size()) - 1);
    QWidget::focusInEvent(e);
    update();
}

void JumpBar::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Left) { m_focus = qMax(0, m_focus - 1); update(); }
    else if (e->key() == Qt::Key_Right) { m_focus = qMin(int(m_crumbs.size()) - 1, m_focus + 1); update(); }
    else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space || e->key() == Qt::Key_Down) { if (m_focus >= 0) openCrumb(m_focus); }
    else QWidget::keyPressEvent(e);
}
