#include "EditorParts.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include "Icons.h"
#include "UiUtil.h"
#include "glass/Glass.h"

ThemedLabel::ThemedLabel(const QString &text, qreal px, QFont::Weight w, Role r, QWidget *parent, bool mono) : QLabel(text, parent), m_role(r) {
    setFont(mono ? Theme::instance().mono(px, w) : Theme::instance().ui(px, w));
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    setCursor(Qt::ArrowCursor);
    applyPalette();
    connect(&Theme::instance(), &Theme::changed, this, [this] { applyPalette(); });
}

void ThemedLabel::setRole(Role r) {
    m_role = r;
    applyPalette();
}

void ThemedLabel::applyPalette() {
    const Tokens &t = tk();
    QColor c = t.text;
    switch (m_role) {
    case Role::Secondary: c = t.textSecondary; break;
    case Role::Tertiary: c = t.textTertiary; break;
    case Role::Fail: c = t.fail; break;
    case Role::Pass: c = t.pass; break;
    case Role::Warn: c = t.warn; break;
    case Role::Accent: c = t.accent; break;
    case Role::Text: break;
    }
    QPalette p = palette();
    p.setColor(QPalette::WindowText, c);
    setPalette(p);
}

// --- PillButton ---------------------------------------------------------------------------------------

PillButton::PillButton(const QString &text, Style s, QWidget *parent, const QString &icon) : QAbstractButton(parent), m_style(s), m_icon(icon) {
    setText(text);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(text);
    setFont(Theme::instance().ui(12, QFont::Medium));
}

QSize PillButton::sizeHint() const {
    const QFontMetrics fm(Theme::instance().ui(12, QFont::Medium));
    return QSize(fm.horizontalAdvance(text()) + 26 + (m_icon.isEmpty() ? 0 : 20), 26);
}

void PillButton::focusInEvent(QFocusEvent *e) {
    QAbstractButton::focusInEvent(e);
    m_kb = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
    update();
}

void PillButton::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) click();
    else QAbstractButton::keyPressEvent(e);
}

void PillButton::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor bg, fg;
    if (m_style == Style::Primary) {
        bg = isDown() ? t.accent.darker(125) : (m_hover ? t.accent.lighter(110) : t.accent);
        fg = t.accentText;
    } else if (m_style == Style::Secondary) {
        bg = isDown() ? t.glassPressed.lighter(100) : (m_hover ? Ui::mix(t.field, t.text, 0.08) : t.field);
        fg = t.text;
    } else {
        bg = isDown() ? t.glassPressed : (m_hover ? t.glassHover : QColor(Qt::transparent));
        fg = t.accent;
    }
    if (!isEnabled()) { bg = m_style == Style::Plain ? QColor(Qt::transparent) : Ui::withAlpha(t.field, 160); fg = t.textTertiary; }
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    p.setFont(Theme::instance().ui(12, QFont::Medium));
    p.setPen(fg);
    int x = 0;
    if (!m_icon.isEmpty()) {
        Icons::paint(&p, m_icon, QRectF(12, height() / 2.0 - 7, 14, 14), fg);
        x = 10;
    }
    p.drawText(rect().adjusted(x, 0, 0, 0), Qt::AlignCenter, text());
    if (m_kb && hasFocus()) Ui::drawFocusRing(&p, r, r.height() / 2);
}

// --- RowButton ----------------------------------------------------------------------------------------

RowButton::RowButton(const QString &icon, const QString &title, const QString &sub, const QString &trailing, QWidget *parent)
    : QAbstractButton(parent), m_icon(icon), m_title(title), m_sub(sub), m_trailing(trailing) {
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(title);
    setAccessibleDescription(sub + " " + trailing);
    setMinimumHeight(sizeHint().height());
}

void RowButton::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) click();
    else QAbstractButton::keyPressEvent(e);
}

void RowButton::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (!m_plain && (m_hover || isDown())) {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() ? t.glassPressed : t.glassHover);
        p.drawRoundedRect(r, 6, 6);
    }
    if (m_bar.isValid()) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_bar);
        p.drawRoundedRect(QRectF(0, 3, 3, height() - 6), 1.5, 1.5);
    }
    int x = m_bar.isValid() ? 12 : 8;
    if (!m_icon.isEmpty()) {
        Icons::paint(&p, m_icon, QRectF(x, height() / 2.0 - 8, 16, 16), m_iconColor.isValid() ? m_iconColor : t.textSecondary);
        x += 24;
    }
    int right = width() - 10;
    if (!m_trailing.isEmpty()) {
        p.setFont(Theme::instance().ui(12));
        const int w = QFontMetrics(p.font()).horizontalAdvance(m_trailing);
        p.setPen(t.textSecondary);
        p.drawText(QRect(right - w, 0, w, height()), Qt::AlignVCenter | Qt::AlignRight, m_trailing);
        right -= w + 12;
    }
    p.setFont(Theme::instance().ui(13));
    p.setPen(t.text);
    const QFontMetrics fm(p.font());
    if (m_sub.isEmpty()) {
        p.drawText(QRect(x, 0, right - x, height()), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_title, Qt::ElideRight, right - x));
    } else {
        p.drawText(QRect(x, 3, right - x, 18), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_title, Qt::ElideRight, right - x));
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textSecondary);
        p.drawText(QRect(x, 21, right - x, 16), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(m_sub, Qt::ElideRight, right - x));
    }
    if (m_kb && hasFocus()) Ui::drawFocusRing(&p, r, 6);
}

// --- StatBar ------------------------------------------------------------------------------------------

void StatBar::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int total = m_p + m_f + m_s;
    p.setPen(Qt::NoPen);
    p.setBrush(Ui::withAlpha(t.text, 18));
    p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    if (total <= 0) return;
    qreal x = 0;
    auto seg = [&](int n, const QColor &c) {
        if (n <= 0) return;
        const qreal w = width() * qreal(n) / total;
        p.setBrush(c);
        p.drawRoundedRect(QRectF(x, 0, qMax<qreal>(w - 1, 2), height()), height() / 2.0, height() / 2.0);
        x += w;
    };
    seg(m_p, t.pass);
    seg(m_f, t.fail);
    seg(m_s, t.textTertiary);
}
