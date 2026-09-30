#include "Toolbar.h"
#include <QPainter>
#include "Icons.h"
#include "UiUtil.h"
#include "glass/Glass.h"

Toolbar::Toolbar(const Actions &a, QWidget *parent) : QWidget(parent) {
    setFixedHeight(Metrics::toolbarHeight);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAccessibleName("Toolbar");
    m_nav = new GlassGroup(this);
    m_nav->setObjectName("toolbar-navigator");
    m_nav->addButton(a.navigator, "sidebar.left");
    m_run = new GlassGroup(this);
    m_run->setObjectName("toolbar-run");
    m_run->addButton(a.stop, "stop.fill");
    m_run->addButton(a.run, "play.fill");
    m_capsule = new Capsule(this);
    m_right = new GlassGroup(this);
    m_right->setObjectName("toolbar-panels");
    m_right->addButton(a.debug, "sidebar.bottom");
    m_right->addButton(a.inspector, "sidebar.right");
    // Tab order follows the layout.
    setTabOrder(m_nav->buttons()[0], m_run->buttons()[0]);
    setTabOrder(m_run->buttons()[0], m_run->buttons()[1]);
    setTabOrder(m_run->buttons()[1], m_capsule);
    setTabOrder(m_capsule, m_right->buttons()[0]);
    setTabOrder(m_right->buttons()[0], m_right->buttons()[1]);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); });
}

void Toolbar::relayout() {
    const int H = height();
    const int pad = 10;   // group shadow margin (3) included in the widget rect
    auto place = [&](QWidget *w, int x) {
        const QSize s = w->sizeHint();
        w->setGeometry(x, (H - s.height()) / 2, s.width(), s.height());
        return x + s.width();
    };
    int x = pad + 2;
    x = place(m_nav, x) + 6;
    x = place(m_run, x) + 12;
    const int leftEnd = x;
    const int rightW = m_right->sizeHint().width();
    place(m_right, width() - pad - 2 - rightW);
    const int rightStart = width() - pad - 2 - rightW;
    // Title (mark + text) between the run group and the capsule, hidden when it does not fit.
    const QFont f = Theme::instance().ui(13, QFont::Bold);
    const int titleW = 20 + QFontMetrics(f).horizontalAdvance("MobileLab") + 8;
    // capsule: centred in the window, limited by both sides
    int capW = qBound(300, width() / 2 - 40, 640);
    int capX = (width() - capW) / 2;
    m_showTitle = capX - leftEnd >= titleW + 10;
    if (!m_showTitle && capX < leftEnd) capX = leftEnd;
    if (capX + capW > rightStart - 10) capW = qMax(240, rightStart - 10 - capX);
    const QSize cs = m_capsule->sizeHint();
    m_capsule->setGeometry(capX - 3, (H - cs.height()) / 2, capW + 6, cs.height());
    m_titleRect = QRect(leftEnd, 0, titleW, H);
    update();
}

void Toolbar::paintEvent(QPaintEvent *) {
    if (Glass::suppressed() && false) return;
    if (!m_showTitle) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Icons::paint(&p, "mobilelab.mark", QRectF(m_titleRect.left(), height() / 2 - 8, 16, 16), tk().accent);
    p.setFont(Theme::instance().ui(13, QFont::Bold));
    p.setPen(tk().text);
    p.drawText(QRect(m_titleRect.left() + 22, 0, m_titleRect.width(), height()), Qt::AlignVCenter | Qt::AlignLeft, "MobileLab");
}
