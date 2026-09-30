#include "UiUtil.h"
#include <QAction>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QWidget>
#include "glass/GlassMath.h"

namespace Ui {

QString shortcutText(const QKeySequence &seq) { return seq.toString(QKeySequence::NativeText); }

void setTip(QWidget *w, const QString &title, const QKeySequence &shortcut) {
    w->setToolTip(shortcut.isEmpty() ? title : QString("%1  (%2)").arg(title, shortcutText(shortcut)));
    if (w->accessibleName().isEmpty()) w->setAccessibleName(title);
}

QColor withAlpha(const QColor &c, int alpha) {
    QColor r = c;
    r.setAlpha(alpha);
    return r;
}

QColor mix(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QVariantAnimation *animate(QObject *parent, qreal from, qreal to, int ms, std::function<void(qreal)> cb,
                           QEasingCurve::Type curve, std::function<void()> done) {
    if (!motionAllowed() || ms <= 0) {
        cb(to);
        if (done) done();
        return nullptr;
    }
    auto *a = new QVariantAnimation(parent);
    a->setStartValue(from);
    a->setEndValue(to);
    a->setDuration(ms);
    a->setEasingCurve(curve);
    QObject::connect(a, &QVariantAnimation::valueChanged, parent, [cb](const QVariant &v) { cb(v.toReal()); });
    QObject::connect(a, &QVariantAnimation::finished, parent, [a, done] {
        if (done) done();
        a->deleteLater();
    });
    a->start();
    return a;
}

void drawSoftShadow(QPainter *p, const QRect &r, int radius, int blur, int offsetY, const QColor &color) {
    if (r.width() < 2 || r.height() < 2 || color.alpha() == 0) return;
    const qreal dpr = p->device()->devicePixelRatioF();
    struct Slice { QImage img; int c; };
    static QHash<QString, Slice> cache;
    const QString key = QString("%1|%2|%3|%4").arg(radius).arg(blur).arg(color.rgba()).arg(dpr);
    auto it = cache.find(key);
    if (it == cache.end()) {
        const int pad = blur * 2, c = radius + pad;             // corner extent in logical px
        const int side = qRound((2 * c + 4) * dpr);
        QImage src(side, side, QImage::Format_ARGB32_Premultiplied);
        src.fill(Qt::transparent);
        QPainter sp(&src);
        sp.setRenderHint(QPainter::Antialiasing);
        sp.setBrush(color);
        sp.setPen(Qt::NoPen);
        sp.drawRoundedRect(QRectF(pad * dpr, pad * dpr, side - 2 * pad * dpr, side - 2 * pad * dpr), radius * dpr, radius * dpr);
        sp.end();
        src = glass::boxBlur(src, qMax(1, int(blur * dpr * 0.5)), 3);
        it = cache.insert(key, Slice{src, c});
        if (cache.size() > 24) cache.clear();
    }
    const QImage &s = it->img;
    const int c = it->c;
    const qreal pc = c * dpr;                       // corner size in source px
    const QRect box = r.adjusted(-2 * blur, -2 * blur + offsetY, 2 * blur, 2 * blur + offsetY);
    const int w = box.width(), h = box.height();
    if (w < 2 * c || h < 2 * c) return;
    const int mid = s.width() - 2 * qRound(pc);
    p->save();
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    auto blit = [&](const QRectF &dst, const QRectF &src) { p->drawImage(dst, s, src); };
    const qreal x0 = box.left(), y0 = box.top();
    blit({x0, y0, double(c), double(c)}, {0, 0, pc, pc});
    blit({x0 + w - c, y0, double(c), double(c)}, {s.width() - pc, 0, pc, pc});
    blit({x0, y0 + h - c, double(c), double(c)}, {0, s.height() - pc, pc, pc});
    blit({x0 + w - c, y0 + h - c, double(c), double(c)}, {s.width() - pc, s.height() - pc, pc, pc});
    if (mid > 0) {
        const QRectF midSrcX(pc, 0, mid, pc), midSrcY(0, pc, pc, mid);
        blit({x0 + c, y0, double(w - 2 * c), double(c)}, midSrcX);
        blit({x0 + c, y0 + h - c, double(w - 2 * c), double(c)}, {pc, s.height() - pc, double(mid), pc});
        blit({x0, y0 + c, double(c), double(h - 2 * c)}, midSrcY);
        blit({x0 + w - c, y0 + c, double(c), double(h - 2 * c)}, {s.width() - pc, pc, pc, double(mid)});
    }
    p->restore();
}

void drawFocusRing(QPainter *p, const QRectF &r, qreal radius) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QColor c = tk().accent;
    c.setAlpha(170);
    p->setPen(QPen(c, 2.0));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(r.adjusted(-1, -1, 1, 1), radius + 1, radius + 1);
    p->restore();
}

QString elide(const QFont &f, const QString &s, int width, Qt::TextElideMode mode) {
    return QFontMetrics(f).elidedText(s, mode, qMax(0, width));
}

QColor statusColor(const QString &state, const Tokens &t) {
    if (state == "running") return t.pass;
    if (state == "booting" || state == "stopping") return t.warn;
    if (state == "error") return t.fail;
    return t.textTertiary;
}

}  // namespace Ui

namespace Ui {
int fuzzyScore(const QString &pattern, const QString &text) {
    if (pattern.isEmpty()) return 0;
    const QString p = pattern.toLower(), t = text.toLower();
    int score = 0, pi = 0, streak = 0;
    for (int i = 0; i < t.size() && pi < p.size(); ++i) {
        if (t[i] != p[pi]) { streak = 0; continue; }
        int s = 10;
        if (streak > 0) s += 8 * streak;                          // consecutive run
        const bool wordStart = i == 0 || !t[i - 1].isLetterOrNumber() || (text[i].isUpper() && text[i - 1].isLower());
        if (wordStart) s += 14;
        if (i == pi) s += 6;                                      // matches from the very start
        score += s;
        ++streak;
        ++pi;
    }
    if (pi < p.size()) return -1;
    return score - qMin(text.size(), 60) / 4;                       // shorter candidates win ties
}
}
