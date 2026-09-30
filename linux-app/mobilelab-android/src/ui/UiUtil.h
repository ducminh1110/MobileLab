#pragma once
// Small shared helpers for the widgets.
#include <QColor>
#include <QKeySequence>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QVariantAnimation>
#include <functional>
#include "Theme.h"

class QWidget;
class QAction;

namespace Ui {
// Tooltip "Title  (Ctrl+0)" plus accessible name.
void setTip(QWidget *w, const QString &title, const QKeySequence &shortcut = {});
QString shortcutText(const QKeySequence &seq);
QColor withAlpha(const QColor &c, int alpha);
QColor mix(const QColor &a, const QColor &b, qreal t);
inline bool motionAllowed() { return !Theme::instance().reducedMotion(); }
// Runs `cb(value)` for value from..to, or immediately with `to` when motion is reduced. Parented to `parent`.
QVariantAnimation *animate(QObject *parent, qreal from, qreal to, int ms, std::function<void(qreal)> cb,
                           QEasingCurve::Type curve = QEasingCurve::InOutCubic, std::function<void()> done = {});
// Soft shadow behind a rounded rect: cached corner/edge slices, no blur per paint.
void drawSoftShadow(QPainter *p, const QRect &r, int radius, int blur, int offsetY, const QColor &color);
void drawFocusRing(QPainter *p, const QRectF &r, qreal radius);
QString elide(const QFont &f, const QString &s, int width, Qt::TextElideMode mode = Qt::ElideRight);
// Fixed-height status dot colours: running, booting, stopped, error.
QColor statusColor(const QString &state, const Tokens &t);
}  // namespace Ui
