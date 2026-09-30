#include "GlassMenu.h"
#include <QApplication>
#include <QPainter>
#include <QScreen>
#include "glass/Glass.h"
#include "Theme.h"

QImage renderPopupMaterial(QWidget *underWindow, const QRect &globalRect, qreal dpr, qreal radius) {
    const Glass::Material mat = Glass::Material::forKind(Glass::Kind::Sheet, tk());
    const Glass::Level lvl = Glass::level();
    const int w = qMax(1, int(globalRect.width() * dpr)), h = qMax(1, int(globalRect.height() * dpr));
    QImage crop;
    QPoint origin;
    if (lvl != Glass::Level::Off && underWindow && underWindow->isVisible()) {
        const int mDev = Glass::backdropMargin(mat, lvl, dpr);
        const int mLog = int(std::ceil(mDev / dpr));
        const QPoint tl = underWindow->mapFromGlobal(globalRect.topLeft());
        // What is visible right now (glass included): the popup floats above it.
        QImage full(QSize(int((globalRect.width() + 2 * mLog) * dpr), int((globalRect.height() + 2 * mLog) * dpr)), QImage::Format_ARGB32_Premultiplied);
        full.setDevicePixelRatio(dpr);
        full.fill(tk().windowTop);
        QPainter p(&full);
        const QRect want = QRect(tl, globalRect.size()).adjusted(-mLog, -mLog, mLog, mLog);
        const QRect inside = want.intersected(underWindow->rect());
        if (!inside.isEmpty()) underWindow->render(&p, inside.topLeft() - want.topLeft(), QRegion(inside), QWidget::DrawWindowBackground | QWidget::DrawChildren);
        p.end();
        crop = full;
        origin = QPoint(int(mLog * dpr), int(mLog * dpr));
    }
    return Glass::renderMaterial(crop, origin, w, h, dpr, radius * dpr, mat, lvl);
}

GlassMenu::GlassMenu(QWidget *parent) : QMenu(parent) { init(); }
GlassMenu::GlassMenu(const QString &title, QWidget *parent) : QMenu(title, parent) { init(); }

void GlassMenu::init() {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::NoDropShadowWindowHint, true);
    setFont(Theme::instance().ui(13));
    setProperty("glassMenu", true);
}

void GlassMenu::showEvent(QShowEvent *e) {
    QMenu::showEvent(e);
    m_materialSize = QSize();
}

void GlassMenu::resizeEvent(QResizeEvent *e) {
    QMenu::resizeEvent(e);
    m_materialSize = QSize();
}

void GlassMenu::paintEvent(QPaintEvent *e) {
    if (m_materialSize != size()) {
        QWidget *under = QApplication::activeWindow();
        if (!under || under == this) under = parentWidget() ? parentWidget()->window() : nullptr;
        m_material = renderPopupMaterial(under, QRect(mapToGlobal(QPoint(0, 0)), size()), devicePixelRatioF(), 12);
        m_materialSize = size();
    }
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(QPoint(0, 0), m_material);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.end();
    QMenu::paintEvent(e);
}
