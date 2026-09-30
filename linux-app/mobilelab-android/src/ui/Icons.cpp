#include "Icons.h"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace Icons {
namespace {
struct Entry { QByteArray svg; };
QHash<QString, QByteArray> &sources() {
    static QHash<QString, QByteArray> s;
    return s;
}
QHash<QString, QPixmap> &cache() {
    static QHash<QString, QPixmap> c;
    return c;
}
const QByteArray *load(const QString &name) {
    auto &s = sources();
    auto it = s.find(name);
    if (it != s.end()) return &it.value();
    QFile f(":/icons/" + name + ".svg");
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    return &s.insert(name, f.readAll()).value();
}
}

bool exists(const QString &name) { return QFile::exists(":/icons/" + name + ".svg"); }

QStringList names() {
    QStringList out;
    for (const auto &e : QDir(":/icons").entryList({"*.svg"})) out << e.left(e.size() - 4);
    return out;
}

void clearCache() { cache().clear(); }

QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr) {
    const QString key = QString("%1|%2|%3|%4").arg(name).arg(size).arg(color.name(QColor::HexArgb)).arg(dpr);
    auto it = cache().find(key);
    if (it != cache().end()) return it.value();
    const QByteArray *src = load(name);
    if (!src) return {};
    QByteArray svg = *src;
    // currentColor -> explicit colour (opacity handled through the paint below)
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer r(svg);
    const int px = qMax(1, qRound(size * dpr));
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, px, px));
    if (color.alpha() < 255) {
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(img.rect(), QColor(0, 0, 0, color.alpha()));
    }
    p.end();
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    if (cache().size() > 1500) cache().clear();
    cache().insert(key, pm);
    return pm;
}

QIcon icon(const QString &name, const QColor &color, int size) {
    QIcon ic;
    ic.addPixmap(pixmap(name, size, color, 1.0));
    ic.addPixmap(pixmap(name, size, color, 2.0));
    return ic;
}

void paint(QPainter *p, const QString &name, const QRectF &target, const QColor &color) {
    const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
    const int size = qRound(qMax(target.width(), target.height()));
    const QPixmap pm = pixmap(name, size, color, dpr);
    if (pm.isNull()) return;
    p->drawPixmap(QRectF(target.center().x() - size / 2.0, target.center().y() - size / 2.0, size, size), pm, QRectF(pm.rect()));
}
}
