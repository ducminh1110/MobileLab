#include "Capsule.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include "Icons.h"
#include "UiUtil.h"

Capsule::Capsule(QWidget *parent) : GlassPanel(parent) {
    setShape(Shape::Capsule);
    setShadowMargin(3);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName("capsule");
    setAccessibleName("Scheme and destination");
    m_spin.setParent(this);
    m_spin.setInterval(50);
    connect(&m_spin, &QTimer::timeout, this, [this] { update(); });
    connect(&Theme::instance(), &Theme::motionChanged, this, &Capsule::updateSpinner);
    m_clock.start();
}

void Capsule::setState(const State &s) {
    m_state = s;
    updateSpinner();
    setAccessibleDescription(accessibleSummary());
    update();
}

QString Capsule::accessibleSummary() const {
    return QString("%1 to %2. %3 %4").arg(m_state.scheme, m_state.destination, m_state.state, m_state.detail).trimmed();
}

void Capsule::updateSpinner() {
    const bool need = m_state.spinning && isVisible() && Ui::motionAllowed();
    if (need && !m_spin.isActive()) m_spin.start();
    else if (!need && m_spin.isActive()) m_spin.stop();
}

void Capsule::showEvent(QShowEvent *e) {
    GlassPanel::showEvent(e);
    updateSpinner();
}
void Capsule::hideEvent(QHideEvent *e) {
    GlassPanel::hideEvent(e);
    updateSpinner();
}

Capsule::Layout Capsule::computeLayout(const QRect &shape) const {
    Layout L;
    const QFont fMed = Theme::instance().ui(12, QFont::Medium), fSemi = Theme::instance().ui(12, QFont::DemiBold), fReg = Theme::instance().ui(12);
    const QFontMetrics mMed(fMed), mSemi(fSemi), mReg(fReg);
    const int h = 26, y = shape.center().y() - h / 2 + 1;
    int x = shape.left() + 8;
    // scheme: mark + name + chevron
    L.schemeText = m_state.scheme;
    const int schemeW = 8 + 16 + 6 + mMed.horizontalAdvance(L.schemeText) + 4;
    L.scheme = QRect(x, y, schemeW, h);
    x += schemeW + 2;
    // separator chevron (8px), then destination
    const int sep = 12;
    x += sep;
    // status (right aligned)
    L.stateText = m_state.state;
    L.detailText = m_state.detail;
    const int spinW = (m_state.spinning || m_state.tone == Tone::Fail || m_state.tone == Tone::Warn) ? 18 : 0;
    L.stateW = mSemi.horizontalAdvance(L.stateText);
    int statusW = spinW + L.stateW + (L.detailText.isEmpty() ? 0 : 12 + mReg.horizontalAdvance(L.detailText)) + 16;
    const int rightPad = 6;
    int maxStatus = shape.right() - rightPad - x - 90;   // keep at least 90px for the destination
    if (statusW > maxStatus) {
        const int detailAvail = qMax(0, maxStatus - spinW - L.stateW - 12 - 16);
        L.detailText = detailAvail > 30 ? mReg.elidedText(m_state.detail, Qt::ElideRight, detailAvail) : QString();
        statusW = spinW + L.stateW + (L.detailText.isEmpty() ? 0 : 12 + mReg.horizontalAdvance(L.detailText)) + 16;
    }
    L.status = QRect(shape.right() - rightPad - statusW, y, statusW, h);
    // destination fills the gap
    const int destAvail = qMax(60, L.status.left() - x - 10);
    L.destText = mMed.elidedText(m_state.destination, Qt::ElideRight, destAvail - 8 - 14 - 6 - 4);
    const int destW = qMin(destAvail, 8 + 14 + 6 + mMed.horizontalAdvance(L.destText) + 4);
    L.destination = QRect(x - 4, y, destW, h);
    return L;
}

QRect Capsule::regionRect(Region r) const {
    const Layout L = computeLayout(shapeRect());
    return r == Scheme ? L.scheme : r == Destination ? L.destination : L.status;
}

Capsule::Region Capsule::regionAt(const QPoint &p) const {
    const Layout L = computeLayout(shapeRect());
    if (L.scheme.contains(p)) return Scheme;
    if (L.destination.contains(p)) return Destination;
    if (L.status.contains(p)) return Status;
    return None;
}

void Capsule::paintContent(QPainter &p, const QRect &shape) {
    const Tokens &t = tk();
    const Layout L = computeLayout(shape);
    p.setRenderHint(QPainter::Antialiasing);
    auto hoverBg = [&](const QRect &r, Region id) {
        if (m_hover == id || m_pressed == id) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pressed == id ? t.glassPressed : t.glassHover);
            p.drawRoundedRect(QRectF(r).adjusted(0, 0, 0, 0), 13, 13);
        }
        if (m_focus == id && hasFocus()) Ui::drawFocusRing(&p, QRectF(r), 13);
    };
    hoverBg(L.scheme, Scheme);
    hoverBg(L.destination, Destination);
    hoverBg(L.status, Status);

    const QFont fMed = Theme::instance().ui(12, QFont::Medium), fSemi = Theme::instance().ui(12, QFont::DemiBold), fReg = Theme::instance().ui(12);
    // scheme
    QRect r = L.scheme;
    Icons::paint(&p, "mobilelab.mark", QRectF(r.left() + 8, r.center().y() - 8, 16, 16), t.accent);
    p.setFont(fMed);
    p.setPen(t.text);
    p.drawText(QRect(r.left() + 8 + 16 + 6, r.top(), r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, L.schemeText);
    // separator chevron
    Icons::paint(&p, "chevron.right", QRectF(L.scheme.right() + 3, shape.center().y() - 4 + 1, 8, 8), t.textTertiary);
    // destination
    r = L.destination;
    Icons::paint(&p, m_state.destinationIcon, QRectF(r.left() + 8, r.center().y() - 7, 14, 14), t.textSecondary);
    p.setPen(t.text);
    p.drawText(QRect(r.left() + 8 + 14 + 6, r.top(), r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, L.destText);
    // status
    r = L.status;
    int x = r.left() + 8;
    const int cy = r.center().y();
    if (m_state.spinning) {
        const qreal ang = (m_clock.elapsed() % 900) / 900.0 * 360.0;
        QPen pen(t.accent, 1.8, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawArc(QRectF(x + 1, cy - 6, 12, 12), int(-ang * 16), 270 * 16);
        x += 18;
    } else if (m_state.tone == Tone::Fail) {
        Icons::paint(&p, "xmark.diamond.fill", QRectF(x, cy - 7, 14, 14), t.fail);
        x += 18;
    } else if (m_state.tone == Tone::Warn) {
        Icons::paint(&p, "exclamationmark.triangle", QRectF(x, cy - 7, 14, 14), t.warn);
        x += 18;
    }
    p.setFont(fSemi);
    p.setPen(t.text);
    p.drawText(QRect(x, r.top(), L.stateW + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, L.stateText);
    x += L.stateW;
    if (!L.detailText.isEmpty()) {
        p.setFont(fReg);
        p.setPen(Ui::withAlpha(t.text, 90));
        p.drawText(QRect(x, r.top(), 12, r.height()), Qt::AlignCenter, "|");
        x += 12;
        p.setPen(t.textSecondary);
        p.drawText(QRect(x, r.top(), r.right() - x, r.height()), Qt::AlignVCenter | Qt::AlignLeft, L.detailText);
    }
}

void Capsule::mouseMoveEvent(QMouseEvent *e) {
    const Region r = regionAt(e->position().toPoint());
    if (r != m_hover) {
        m_hover = r;
        setCursor(r == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void Capsule::mousePressEvent(QMouseEvent *e) {
    m_pressed = regionAt(e->position().toPoint());
    update();
}

void Capsule::mouseReleaseEvent(QMouseEvent *e) {
    const Region r = regionAt(e->position().toPoint());
    const Region pressed = m_pressed;
    m_pressed = None;
    update();
    if (r != None && r == pressed) activate(r);
}

void Capsule::leaveEvent(QEvent *) {
    m_hover = None;
    update();
}

void Capsule::activate(Region r) {
    const QRect rr = regionRect(r);
    const QPoint below = mapToGlobal(QPoint(rr.left(), rr.bottom() + 4));
    if (r == Scheme) emit schemeRequested(below);
    else if (r == Destination) emit destinationRequested(below);
    else if (r == Status) emit statusRequested();
}

void Capsule::focusInEvent(QFocusEvent *e) {
    if (m_focus == None) m_focus = Scheme;
    GlassPanel::focusInEvent(e);
    update();
}
void Capsule::focusOutEvent(QFocusEvent *e) {
    GlassPanel::focusOutEvent(e);
    update();
}

void Capsule::keyPressEvent(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_Left: m_focus = Region(qMax(0, int(m_focus) - 1)); update(); break;
    case Qt::Key_Right: m_focus = Region(qMin(2, int(m_focus) + 1)); update(); break;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: case Qt::Key_Down: activate(m_focus); break;
    default: GlassPanel::keyPressEvent(e);
    }
}
