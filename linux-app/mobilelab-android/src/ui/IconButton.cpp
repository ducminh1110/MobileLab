#include "IconButton.h"
#include <QAction>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include "Icons.h"
#include "Theme.h"
#include "UiUtil.h"
#include "glass/Glass.h"

IconButton::IconButton(const QString &icon, QWidget *parent) : QAbstractButton(parent), m_icon(icon) {
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(m_size);
}

void IconButton::setIconName(const QString &name) {
    m_icon = name;
    update();
}

void IconButton::setFixedButtonSize(int w, int h) {
    m_size = QSize(w, h);
    setFixedSize(m_size);
}

void IconButton::bindAction(QAction *a, const QString &tooltipTitle) {
    m_action = a;
    m_tipTitle = tooltipTitle;
    setCheckable(a->isCheckable());
    connect(a, &QAction::changed, this, &IconButton::syncFromAction);
    connect(this, &QAbstractButton::clicked, a, [a] { a->trigger(); });
    syncFromAction();
}

void IconButton::syncFromAction() {
    if (!m_action) return;
    setEnabled(m_action->isEnabled());
    setChecked(m_action->isChecked());
    const QString title = !m_tipTitle.isEmpty() ? m_tipTitle : QString(m_action->text()).remove('&');
    Ui::setTip(this, title, m_action->shortcut());
    setAccessibleName(title);
    update();
}

void IconButton::focusInEvent(QFocusEvent *e) {
    m_kbFocus = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
    QAbstractButton::focusInEvent(e);
    update();
}

void IconButton::focusOutEvent(QFocusEvent *e) {
    m_kbFocus = false;
    QAbstractButton::focusOutEvent(e);
    update();
}

void IconButton::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        click();
        return;
    }
    QAbstractButton::keyPressEvent(e);
}

void IconButton::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal radius = m_circular ? qMin(r.width(), r.height()) / 2 : qMin<qreal>(qMin(r.width(), r.height()) / 2, 7);
    if (m_hoverBg && isEnabled() && (isDown() || m_hover)) {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() ? t.glassPressed : t.glassHover);
        p.drawRoundedRect(r, radius, radius);
    }
    QColor c = m_tint.isValid() ? m_tint : t.text;
    if (isCheckable() && isChecked()) {
        if (m_checkedTint.isValid()) c = m_checkedTint;
        else if (m_accentChecked) c = t.accent;
    }
    if (!isEnabled()) c = t.textTertiary;
    Icons::paint(&p, m_icon, QRectF(0, 0, width(), height()).adjusted((width() - m_glyph) / 2.0, (height() - m_glyph) / 2.0, -(width() - m_glyph) / 2.0, -(height() - m_glyph) / 2.0), c);
    if (m_kbFocus && hasFocus()) Ui::drawFocusRing(&p, r, radius);
}
