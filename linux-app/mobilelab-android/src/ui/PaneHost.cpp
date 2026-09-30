#include "PaneHost.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "UiUtil.h"
#include "glass/Glass.h"

// --- Panel --------------------------------------------------------------------------------------------

Panel::Panel(Role r, QWidget *parent) : QWidget(parent), m_role(r) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); });
}

QColor Panel::fillColor() const { return m_role == Role::Sidebar ? tk().sidebar : tk().editor; }

void Panel::paintEvent(QPaintEvent *) {
    if (Glass::suppressed() && false) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(fillColor());
    p.setPen(QPen(tk().panelBorder, 1));
    p.drawRoundedRect(r, Metrics::panelRadius, Metrics::panelRadius);
}

// --- PaneHandle ---------------------------------------------------------------------------------------

PaneHandle::PaneHandle(PaneHost *host, int id, QWidget *parent) : QWidget(parent), m_host(host), m_id(id) {
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setAccessibleName(host->spec(id).name + " resize handle");
    setToolTip("Drag to resize, double-click to hide");
}

void PaneHandle::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_drag = true;
    m_press = e->globalPosition().toPoint();
    m_startSize = m_host->isShown(m_id) ? m_host->paneSize(m_id) : 0;
}

void PaneHandle::mouseMoveEvent(QMouseEvent *e) {
    if (!m_drag) return;
    const QPoint d = e->globalPosition().toPoint() - m_press;
    const bool horizontal = property("horizontal").toBool();
    const int delta = horizontal ? d.x() : d.y();
    // Leading panes grow when the handle moves right/down, trailing panes when it moves left/up.
    const bool leading = property("leading").toBool();
    m_host->dragPane(m_id, m_startSize + (leading ? delta : -delta));
}

void PaneHandle::mouseReleaseEvent(QMouseEvent *) {
    if (m_drag) m_host->endDrag(m_id);
    m_drag = false;
}

void PaneHandle::mouseDoubleClickEvent(QMouseEvent *) {
    m_drag = false;
    m_host->toggle(m_id);
}

void PaneHandle::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    if (property("divider").toBool()) {
        QPainter p(this);
        p.setPen(tk().divider);
        if (property("horizontal").toBool()) p.drawLine(width() / 2, 0, width() / 2, height());
        else p.drawLine(0, height() / 2, width(), height() / 2);
        if (m_hover || m_drag) {
            p.setPen(Qt::NoPen);
            p.setBrush(Ui::withAlpha(tk().accent, 90));
            if (property("horizontal").toBool()) p.drawRect(width() / 2 - 1, 0, 2, height());
            else p.drawRect(0, height() / 2 - 1, width(), 2);
        }
    }
}

// --- PaneHost -----------------------------------------------------------------------------------------

PaneHost::PaneHost(Orientation o, QWidget *parent) : QWidget(parent), m_o(o) {
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void PaneHost::setFloating(bool floating, int margin, int gap) {
    m_floating = floating;
    m_margin = margin;
    m_gap = gap;
    layoutPanes();
}

void PaneHost::setCenter(QWidget *w, int minSize) {
    m_center = w;
    m_centerMin = minSize;
    w->setParent(this);
    layoutPanes();
}

int PaneHost::addPane(Side side, QWidget *w, const Spec &spec) {
    PaneState s;
    s.w = w;
    s.spec = spec;
    s.side = side;
    s.size = spec.defaultSize;
    w->setParent(this);
    const int id = m_panes.size();
    m_panes.push_back(s);
    auto *h = new PaneHandle(this, id, this);
    h->setCursor(m_o == Orientation::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    h->setProperty("leading", side == Side::Leading);
    h->setProperty("horizontal", m_o == Orientation::Horizontal);
    h->setProperty("divider", !m_floating);
    m_panes[id].handle = h;
    layoutPanes();
    return id;
}

void PaneHost::setPaneSize(int id, int px) {
    auto &s = m_panes[id];
    s.size = qBound(s.spec.minSize, px, s.spec.maxSize);
    layoutPanes();
}

int PaneHost::currentSize(int id) const { return int(std::lround(m_panes[id].size * m_panes[id].t)); }

void PaneHost::setShown(int id, bool shown, bool animate) {
    auto &s = m_panes[id];
    const bool changed = s.shown != shown;
    s.shown = shown;
    animateTo(id, shown ? 1.0 : 0.0, animate);
    if (changed) emit shownChanged(id, shown);
}

void PaneHost::animateTo(int id, qreal target, bool animate) {
    auto &s = m_panes[id];
    if (s.anim) {
        s.anim->stop();
        s.anim->deleteLater();
        s.anim = nullptr;
    }
    if (!animate || !Ui::motionAllowed() || !isVisible()) {
        s.t = target;
        layoutPanes();
        return;
    }
    s.anim = Ui::animate(this, s.t, target, target > s.t ? 220 : 180, [this, id](qreal v) {
        m_panes[id].t = v;
        layoutPanes();
    }, QEasingCurve::OutCubic, [this, id, target] {
        m_panes[id].anim = nullptr;
        m_panes[id].t = target;
        layoutPanes();
    });
}

void PaneHost::dragPane(int id, int raw) {
    auto &s = m_panes[id];
    const int snapDistance = int(s.spec.minSize * 0.35);
    if (raw < s.spec.minSize - snapDistance) {
        if (s.shown) setShown(id, false, true);   // snap closed
    } else {
        const int size = qBound(s.spec.minSize, raw, s.spec.maxSize);
        const bool reopen = !s.shown;
        s.size = size;
        if (reopen) setShown(id, true, true);
        else {
            s.t = 1.0;
            layoutPanes();
        }
    }
}

void PaneHost::endDrag(int id) {
    if (m_panes[id].shown) emit sizeChanged(id, m_panes[id].size);
}

void PaneHost::layoutPanes() {
    if (!m_center) return;
    const bool horiz = m_o == Orientation::Horizontal;
    const int total = horiz ? width() : height();
    const int cross = horiz ? height() : width();
    const int margin = m_floating ? m_margin : 0;
    const int gap = m_gap;
    // Effective sizes, shrunk if the centre would get too small.
    QVector<int> eff(m_panes.size());
    int used = 0, gaps = 0;
    for (int i = 0; i < m_panes.size(); ++i) {
        eff[i] = int(std::lround(m_panes[i].size * m_panes[i].t));
        if (eff[i] > 0) { used += eff[i]; gaps += int(std::lround(gap * m_panes[i].t)); }
    }
    const int avail = total - 2 * margin - gaps;
    int overflow = m_centerMin - (avail - used);
    for (int pass = 0; overflow > 0 && pass < 2; ++pass) {
        for (int i = m_panes.size() - 1; i >= 0 && overflow > 0; --i) {
            const int minEff = int(std::lround(m_panes[i].spec.minSize * m_panes[i].t));
            const int give = qMin(overflow, qMax(0, eff[i] - minEff));
            eff[i] -= give;
            overflow -= give;
        }
    }
    // Leading panes from the start, trailing panes from the end, the centre in between.
    int lead = margin, trail = total - margin;
    for (int i = 0; i < m_panes.size(); ++i) {
        auto &s = m_panes[i];
        if (s.side != Side::Leading) continue;
        const int g = int(std::lround(gap * s.t));
        const int sz = eff[i];
        const bool vis = sz > 0 || s.t > 0.001;
        s.w->setVisible(vis);
        s.handle->setVisible(vis && s.shown);
        if (horiz) s.w->setGeometry(lead, margin, sz, cross - (m_floating ? 2 * margin : 0));
        else s.w->setGeometry(0, lead, cross, sz);
        const QRect hr = horiz ? QRect(lead + sz + g / 2 - 4, margin, 8, cross - 2 * margin) : QRect(0, lead + sz + g / 2 - 3, cross, 7);
        s.handle->setGeometry(hr);
        s.handle->raise();
        lead += sz + g;
    }
    for (int i = m_panes.size() - 1; i >= 0; --i) {
        auto &s = m_panes[i];
        if (s.side != Side::Trailing) continue;
        const int g = int(std::lround(gap * s.t));
        const int sz = eff[i];
        const bool vis = sz > 0 || s.t > 0.001;
        s.w->setVisible(vis);
        s.handle->setVisible(vis && s.shown);
        trail -= sz;
        if (horiz) s.w->setGeometry(trail, margin, sz, cross - (m_floating ? 2 * margin : 0));
        else s.w->setGeometry(0, trail, cross, sz);
        const QRect hr = horiz ? QRect(trail - g + g / 2 - 4, margin, 8, cross - 2 * margin) : QRect(0, trail - g + g / 2 - 3, cross, 7);
        s.handle->setGeometry(hr);
        s.handle->raise();
        trail -= g;
    }
    const int csz = qMax(0, trail - lead);
    if (horiz) m_center->setGeometry(lead, margin, csz, cross - (m_floating ? 2 * margin : 0));
    else m_center->setGeometry(0, lead, cross, csz);
    update();
}

void PaneHost::paintEvent(QPaintEvent *) {
    if (Glass::suppressed() && false) return;
    if (!m_floating) return;
    QPainter p(this);
    const Tokens &t = tk();
    auto shadow = [&](const QRect &r) { Ui::drawSoftShadow(&p, r, Metrics::panelRadius, 8, 2, t.shadow); };
    for (const auto &s : m_panes)
        if (s.w->isVisible() && s.w->width() > 8) shadow(s.w->geometry());
    if (m_center) shadow(m_center->geometry());
}
