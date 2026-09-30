#include "Widgets.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include "GlassMenu.h"
#include "Icons.h"
#include "UiUtil.h"

// --- GlassLineEdit ------------------------------------------------------------------------------------

GlassLineEdit::GlassLineEdit(QWidget *parent) : QLineEdit(parent) {
    setFrame(false);
    setProperty("bare", true);
    setFont(Theme::instance().ui(12));
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setClearButtonEnabled(false);
}

void GlassLineEdit::paintEvent(QPaintEvent *e) {
    if (Glass::suppressed()) return;
    QLineEdit::paintEvent(e);
}

// --- FilterBar ----------------------------------------------------------------------------------------

FilterBar::FilterBar(QWidget *parent) : GlassPanel(parent, Glass::Kind::Field) {
    setShape(Shape::Capsule);
    setShadowMargin(2);
    setObjectName("filterbar");
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(shadowMargin() + 24, shadowMargin() + 1, shadowMargin() + 4, shadowMargin() + 1);
    l->setSpacing(2);
    m_edit = new GlassLineEdit(this);
    m_edit->setPlaceholderText("Filter");
    { QPalette pl = m_edit->palette(); pl.setColor(QPalette::PlaceholderText, Ui::withAlpha(tk().text, 120)); m_edit->setPalette(pl); }
    m_edit->setAccessibleName("Filter");
    l->addWidget(m_edit, 1);
    m_clear = new IconButton("xmark.circle.fill", this);
    m_clear->setFixedButtonSize(18, 18);
    m_clear->setGlyphSize(13);
    m_clear->setTint(tk().textTertiary);
    m_clear->setAccessibleName("Clear filter");
    m_clear->setToolTip("Clear filter");
    m_clear->hide();
    l->addWidget(m_clear);
    connect(m_clear, &QAbstractButton::clicked, m_edit, &QLineEdit::clear);
    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_clear->setVisible(!t.isEmpty());
        emit textChanged(t);
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] { m_clear->setTint(tk().textTertiary); });
    setFocusProxy(m_edit);
}

IconButton *FilterBar::addToggle(const QString &icon, const QString &tip, bool checked) {
    auto *b = new IconButton(icon, this);
    b->setFixedButtonSize(22, 22);
    b->setGlyphSize(14);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setTint(tk().textSecondary);
    Ui::setTip(b, tip);
    layout()->addWidget(b);
    const int idx = m_toggles.size();
    m_toggles << b;
    connect(b, &QAbstractButton::toggled, this, [this, idx](bool on) { emit toggled(idx, on); });
    return b;
}

void FilterBar::paintContent(QPainter &p, const QRect &shape) {
    Icons::paint(&p, "line.3.horizontal.decrease.circle", QRectF(shape.left() + 8, shape.center().y() - 8, 16, 16), tk().textSecondary);
    if (m_edit->hasFocus()) Ui::drawFocusRing(&p, QRectF(shape), shape.height() / 2.0);
}

// --- SegmentedControl ---------------------------------------------------------------------------------

SegmentedControl::SegmentedControl(const QStringList &labels, QWidget *parent) : GlassPanel(parent), m_labels(labels) {
    setShape(Shape::Capsule);
    setShadowMargin(3);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName("segmented");
    setAccessibleName("View");
}

QSize SegmentedControl::sizeHint() const {
    const QFontMetrics fm(Theme::instance().ui(12, QFont::Medium));
    int w = 0;
    for (const auto &l : m_labels) w += fm.horizontalAdvance(l) + 28;
    return QSize(w + 2 * (shadowMargin() + 2), 26 + 2 * shadowMargin());
}

QRect SegmentedControl::segmentRect(int i) const {
    const QFontMetrics fm(Theme::instance().ui(12, QFont::Medium));
    const QRect sr = shapeRect().adjusted(2, 2, -2, -2);
    int total = 0;
    QVector<int> ws;
    for (const auto &l : m_labels) { ws << fm.horizontalAdvance(l) + 28; total += ws.last(); }
    const qreal scale = total > 0 ? qreal(sr.width()) / total : 1;
    int x = sr.left();
    for (int k = 0; k < i; ++k) x += int(ws[k] * scale);
    return QRect(x, sr.top(), i == m_labels.size() - 1 ? sr.right() - x + 1 : int(ws[i] * scale), sr.height());
}

void SegmentedControl::setCurrent(int i, bool emitSignal) {
    if (i < 0 || i >= m_labels.size() || i == m_current) return;
    m_current = i;
    update();
    if (emitSignal) emit currentChanged(i);
}

void SegmentedControl::paintContent(QPainter &p, const QRect &shape) {
    Q_UNUSED(shape);
    const Tokens &t = tk();
    p.setRenderHint(QPainter::Antialiasing);
    const QRect sel = segmentRect(m_current);
    // Raised selected segment.
    p.setPen(QPen(t.dark ? Ui::withAlpha(Qt::white, 30) : Ui::withAlpha(Qt::black, 20), 1));
    p.setBrush(t.dark ? QColor(255, 255, 255, 46) : QColor(255, 255, 255, 235));
    p.drawRoundedRect(QRectF(sel).adjusted(0.5, 0.5, -0.5, -0.5), sel.height() / 2.0, sel.height() / 2.0);
    for (int i = 0; i < m_labels.size(); ++i) {
        const QRect r = segmentRect(i);
        p.setFont(Theme::instance().ui(12, i == m_current ? QFont::DemiBold : QFont::Medium));
        p.setPen(i == m_current ? t.text : t.textSecondary);
        p.drawText(r, Qt::AlignCenter, m_labels[i]);
    }
    if (hasFocus()) Ui::drawFocusRing(&p, QRectF(segmentRect(m_current)), segmentRect(m_current).height() / 2.0);
}

void SegmentedControl::mousePressEvent(QMouseEvent *e) {
    for (int i = 0; i < m_labels.size(); ++i)
        if (segmentRect(i).contains(e->position().toPoint())) setCurrent(i, true);
}

void SegmentedControl::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Left) setCurrent(m_current - 1, true);
    else if (e->key() == Qt::Key_Right) setCurrent(m_current + 1, true);
    else GlassPanel::keyPressEvent(e);
}

// --- PopupButton --------------------------------------------------------------------------------------

PopupButton::PopupButton(QWidget *parent) : QAbstractButton(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setFont(Theme::instance().ui(12));
    connect(this, &QAbstractButton::clicked, this, &PopupButton::openMenu);
}

void PopupButton::setItems(const QStringList &items, int current) {
    m_items = items;
    m_current = qBound(0, current, qMax(0, items.size() - 1));
    updateGeometry();
    update();
}

void PopupButton::setCurrentIndex(int i, bool emitSignal) {
    if (i < 0 || i >= m_items.size() || i == m_current) return;
    m_current = i;
    update();
    if (emitSignal) emit currentChanged(i);
}

QSize PopupButton::sizeHint() const {
    const QFontMetrics fm(Theme::instance().ui(12));
    int w = 0;
    for (const auto &s : m_items) w = qMax(w, fm.horizontalAdvance(s));
    return QSize(w + (m_flat ? 22 : 34), 22);
}

void PopupButton::openMenu() {
    GlassMenu menu(this);
    for (int i = 0; i < m_items.size(); ++i) {
        QAction *a = menu.addAction(m_items[i]);
        a->setCheckable(true);
        a->setChecked(i == m_current);
        connect(a, &QAction::triggered, this, [this, i] { setCurrentIndex(i, true); });
    }
    menu.exec(mapToGlobal(QPoint(0, height() + 2)));
}

void PopupButton::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) openMenu();
    else QAbstractButton::keyPressEvent(e);
}

void PopupButton::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (!m_flat || underMouse() || isDown()) {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() ? t.glassPressed : (m_flat ? t.glassHover : t.field));
        p.drawRoundedRect(r, 6, 6);
    }
    p.setFont(Theme::instance().ui(12));
    p.setPen(isEnabled() ? t.text : t.textTertiary);
    p.drawText(QRect(8, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, Ui::elide(font(), currentText(), width() - 26));
    Icons::paint(&p, "chevron.up.chevron.down", QRectF(width() - 16, height() / 2.0 - 5, 10, 10), t.textSecondary);
    if (hasFocus() && !isDown()) Ui::drawFocusRing(&p, r, 6);
}

// --- EmptyState ---------------------------------------------------------------------------------------

EmptyState::EmptyState(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_NoSystemBackground, true);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); });
}

void EmptyState::setContent(const QString &icon, const QString &title, const QString &body, const QString &action) {
    m_icon = icon;
    m_title = title;
    m_body = body;
    m_action = action;
    setAccessibleName(title);
    setAccessibleDescription(body);
    setFocusPolicy(action.isEmpty() ? Qt::NoFocus : Qt::TabFocus);
    update();
}

QRect EmptyState::actionRect() const {
    if (m_action.isEmpty()) return {};
    const QFontMetrics fm(Theme::instance().ui(12, QFont::DemiBold));
    const int w = fm.horizontalAdvance(m_action) + 28;
    return QRect((width() - w) / 2, height() / 2 + 46, w, 26);
}

void EmptyState::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int cx = width() / 2, top = height() / 2 - 70;
    if (!m_icon.isEmpty()) Icons::paint(&p, m_icon, QRectF(cx - 16, top, 32, 32), t.textTertiary);
    p.setFont(Theme::instance().ui(13, QFont::DemiBold));
    p.setPen(t.textSecondary);
    p.drawText(QRect(12, top + 40, width() - 24, 20), Qt::AlignHCenter | Qt::AlignVCenter, m_title);
    p.setFont(Theme::instance().ui(12));
    p.setPen(t.textTertiary);
    p.drawText(QRect(20, top + 62, width() - 40, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_body);
    const QRect ar = actionRect();
    if (!ar.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_down ? t.accent.darker(120) : (m_hover ? t.accent.lighter(110) : t.accent));
        p.drawRoundedRect(ar, 13, 13);
        p.setFont(Theme::instance().ui(12, QFont::DemiBold));
        p.setPen(t.accentText);
        p.drawText(ar, Qt::AlignCenter, m_action);
        if (hasFocus()) Ui::drawFocusRing(&p, QRectF(ar), 13);
    }
}

void EmptyState::mouseMoveEvent(QMouseEvent *e) {
    const bool h = actionRect().contains(e->position().toPoint());
    if (h != m_hover) { m_hover = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void EmptyState::mousePressEvent(QMouseEvent *e) {
    m_down = actionRect().contains(e->position().toPoint());
    update();
}
void EmptyState::mouseReleaseEvent(QMouseEvent *e) {
    const bool fire = m_down && actionRect().contains(e->position().toPoint());
    m_down = false;
    update();
    if (fire) emit actionTriggered();
}
void EmptyState::keyPressEvent(QKeyEvent *e) {
    if (!m_action.isEmpty() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space || e->key() == Qt::Key_Enter)) emit actionTriggered();
    else QWidget::keyPressEvent(e);
}
