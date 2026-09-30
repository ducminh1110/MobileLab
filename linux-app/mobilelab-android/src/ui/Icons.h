#pragma once
// SF-Symbols-like icon set (resources/icons/*.svg), tinted at runtime through `currentColor`.
#include <QIcon>
#include <QPixmap>

namespace Icons {
// Renders `name` (for example "sidebar.left") at `size` logical pixels in `color`.
QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr = 1.0);
QIcon icon(const QString &name, const QColor &color, int size = 16);
void paint(QPainter *p, const QString &name, const QRectF &target, const QColor &color);
bool exists(const QString &name);
QStringList names();
void clearCache();
}
