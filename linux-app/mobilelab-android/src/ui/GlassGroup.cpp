#include "GlassGroup.h"
#include <QPainter>

GlassGroup::GlassGroup(QWidget *parent, Glass::Kind kind) : GlassPanel(parent, kind) {
    setShape(Shape::Capsule);
    setShadowMargin(3);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(shadowMargin() + 2, shadowMargin() + 1, shadowMargin() + 2, shadowMargin() + 1);
    m_layout->setSpacing(0);
}

IconButton *GlassGroup::addButton(QAction *action, const QString &icon, const QString &tooltip) {
    auto *b = new IconButton(icon, this);
    b->setFixedButtonSize(m_bw, m_bh);
    b->bindAction(action, tooltip);
    m_layout->addWidget(b);
    m_buttons << b;
    updateGeometry();
    return b;
}

IconButton *GlassGroup::addButton(const QString &icon) {
    auto *b = new IconButton(icon, this);
    b->setFixedButtonSize(m_bw, m_bh);
    m_layout->addWidget(b);
    m_buttons << b;
    updateGeometry();
    return b;
}

void GlassGroup::setButtonSize(int w, int h) {
    m_bw = w;
    m_bh = h;
    for (auto *b : std::as_const(m_buttons)) b->setFixedButtonSize(w, h);
    updateGeometry();
}

QSize GlassGroup::sizeHint() const {
    const int n = m_buttons.size();
    return QSize(n * m_bw + 2 * (shadowMargin() + 2), m_bh + 2 * (shadowMargin() + 1));
}

void GlassGroup::paintContent(QPainter &p, const QRect &) {
    // Hairlines between neighbouring buttons.
    p.setPen(QPen(Ui::withAlpha(tk().text, 30), 1));
    for (int i = 0; i + 1 < m_buttons.size(); ++i) {
        const QRect a = m_buttons[i]->geometry();
        const int x = a.right() + 1;
        p.drawLine(x, a.top() + 6, x, a.bottom() - 6);
    }
}
