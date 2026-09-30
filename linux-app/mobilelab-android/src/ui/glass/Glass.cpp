#include "Glass.h"
#include <QApplication>
#include <QLinearGradient>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSettings>
#include <cmath>

namespace Glass {

namespace {
int g_suppress = 0;
QMutex g_statsMutex;
QHash<QString, Stats> g_stats;

quint64 mix(quint64 h, quint64 v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}
}

bool suppressed() { return g_suppress > 0; }

Level parseLevel(const QString &s, Level fallback) {
    const QString l = s.trimmed().toLower();
    if (l == "off" || l == "opaque" || l == "0") return Level::Off;
    if (l == "blur" || l == "reduced" || l == "1") return Level::Blur;
    if (l == "full" || l == "on" || l == "2") return Level::Full;
    return fallback;
}

QString levelName(Level l) { return l == Level::Off ? "off" : l == Level::Blur ? "blur" : "full"; }

Settings::Settings() {
    const QByteArray env = qgetenv("MOBILELAB_GLASS");
    if (!env.isEmpty()) {
        m_level = parseLevel(QString::fromLatin1(env), Level::Full);
        m_envOverride = true;
    }
    if (qEnvironmentVariableIntValue("MOBILELAB_REDUCE_TRANSPARENCY") == 1) {
        m_level = Level::Off;
        m_envOverride = true;
    }
}

Settings &Settings::instance() {
    static Settings s;
    return s;
}

void Settings::setLevel(Level l) {
    if (l == m_level) return;
    m_level = l;
    emit levelChanged();
}

void Settings::load(QSettings &s) {
    if (m_envOverride) return;
    m_level = parseLevel(s.value("appearance/glass", "full").toString(), Level::Full);
}

void Settings::save(QSettings &s) const { s.setValue("appearance/glass", levelName(m_level)); }

Material Material::forKind(Kind k, const Tokens &t) {
    Material m;
    m.tintTop = t.glassTintTop;
    m.tintBottom = t.glassTintBottom;
    m.rimLight = t.glassRimLight;
    m.rimDark = t.glassRimDark;
    m.sheen = t.glassSheen;
    m.opaque = t.glassOpaque;
    switch (k) {
    case Kind::Control:
        break;
    case Kind::Accent: {
        QColor top = t.accent.lighter(112), bottom = t.accent.darker(108);
        top.setAlpha(238);
        bottom.setAlpha(232);
        m.tintTop = top;
        m.tintBottom = bottom;
        m.rimLight = QColor(255, 255, 255, t.dark ? 90 : 150);
        m.rimDark = QColor(0, 0, 0, 40);
        m.sheen = QColor(255, 255, 255, 70);
        m.params.edge = 5;
        m.params.rim = 2;
        m.params.blur = 6;
        m.opaque = t.accent;
        break;
    }
    case Kind::Field: {
        QColor f = t.field;
        m.tintTop = QColor(f.red(), f.green(), f.blue(), 210);
        m.tintBottom = QColor(f.red(), f.green(), f.blue(), 190);
        m.rimLight = QColor(255, 255, 255, t.dark ? 22 : 120);
        m.rimDark = QColor(0, 0, 0, t.dark ? 90 : 26);
        m.sheen = QColor(255, 255, 255, t.dark ? 10 : 40);
        m.params.edge = 6;
        m.params.rim = 3;
        m.opaque = t.field;
        break;
    }
    case Kind::Sheet: {
        QColor a = t.dark ? QColor(48, 48, 54, 226) : QColor(250, 250, 252, 222);
        QColor b = t.dark ? QColor(34, 34, 40, 232) : QColor(244, 246, 250, 232);
        m.tintTop = a;
        m.tintBottom = b;
        m.params.blur = 18;
        m.params.edge = 7;
        m.params.rim = 3;
        m.sheenHeight = 0.25;
        m.opaque = t.dark ? QColor("#2c2c31") : QColor("#f7f7f9");
        break;
    }
    }
    return m;
}

QHash<QString, Stats> allStats() {
    QMutexLocker l(&g_statsMutex);
    return g_stats;
}
void resetStats() {
    QMutexLocker l(&g_statsMutex);
    g_stats.clear();
}
static void recordStats(const QString &name, double totalMs, double materialMs, bool recomputed) {
    QMutexLocker l(&g_statsMutex);
    Stats &s = g_stats[name];
    s.paints++;
    s.totalMs += totalMs;
    s.lastMs = totalMs;
    s.maxMs = std::max(s.maxMs, totalMs);
    if (recomputed) {
        s.recomputes++;
        s.materialTotalMs += materialMs;
        s.materialLastMs = materialMs;
        s.materialMaxMs = std::max(s.materialMaxMs, materialMs);
    }
}

static int blurRadiusDev(const Material &m, qreal dpr) { return qMax(1, int(std::lround(m.params.blur * 0.5 * dpr))); }

int backdropMargin(const Material &m, Level level, qreal dpr) {
    const float disp = level == Level::Full && m.params.refract ? m.params.maxDisplacement() : 0.f;
    return int(std::ceil(disp * dpr)) + 2 * blurRadiusDev(m, dpr) + 2;
}

QImage renderMaterial(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev, const Material &m, Level level) {
    glass::Params params = m.params;
    params.blur *= float(dpr);
    params.edge *= float(dpr);
    params.edgeWidth *= float(dpr);
    params.rim *= float(dpr);
    params.rimWidth *= float(dpr);
    params.base *= float(dpr);
    params.baseWidth *= float(dpr);
    params.refract = level == Level::Full && m.params.refract;
    auto table = glass::RefractionTable::cached(w, h, float(radiusDev), params);
    QImage img;
    if (level == Level::Off) {
        img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(m.opaque);
    } else if (backdrop.isNull()) {
        // No backdrop available: tint + rim only, opaque enough to keep text contrast.
        img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        QColor base = m.opaque;
        base.setAlpha(244);
        img.fill(base);
    } else {
        QImage blurred = glass::boxBlur(backdrop, blurRadiusDev(m, dpr), 2);
        glass::saturate(blurred, m.params.saturate);
        if (params.refract) {
            // Rim band: barely blurred, so refracted detail stays crisp at the edge.
            QImage crisp = glass::boxBlur(backdrop, qMax(1, blurRadiusDev(m, dpr) / 4), 1);
            glass::saturate(crisp, m.params.saturate);
            img = glass::refract(blurred, origin, *table, true, &crisp);
        } else {
            img = glass::refract(blurred, origin, *table, false);
        }
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    if (level != Level::Off) {
        QLinearGradient tint(0, 0, 0, h);
        tint.setColorAt(0, m.tintTop);
        tint.setColorAt(1, m.tintBottom);
        p.fillRect(QRect(0, 0, w, h), tint);
        if (m.sheen.alpha() > 0) {
            // Specular sheen: a diagonal gradient hugging the top edge, clipped to the shape.
            QPainterPath clip;
            clip.addRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), radiusDev, radiusDev);
            p.save();
            p.setClipPath(clip);
            QLinearGradient sh(0, 0, w * 0.22, h * m.sheenHeight);
            sh.setColorAt(0, m.sheen);
            QColor none = m.sheen;
            none.setAlpha(0);
            sh.setColorAt(1, none);
            p.fillRect(QRectF(0, 0, w, h), sh);
            p.restore();
        }
    }
    if (m.rim) {
        // Rim light: brighter at the top left (light source), dimmer bottom right.
        const qreal wpx = std::max<qreal>(1.0, dpr);
        QLinearGradient rim(0, 0, w, h);
        rim.setColorAt(0, m.rimLight);
        rim.setColorAt(0.5, level == Level::Off ? m.rimDark : QColor(m.rimLight.red(), m.rimLight.green(), m.rimLight.blue(), m.rimLight.alpha() / 3));
        rim.setColorAt(1, m.rimDark);
        QPen pen(QBrush(rim), wpx);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(wpx / 2, wpx / 2, w - wpx, h - wpx), radiusDev - wpx / 2, radiusDev - wpx / 2);
    }
    p.end();
    glass::applyMask(img, *table);
    img.setDevicePixelRatio(dpr);
    return img;
}

// --- BackdropHub --------------------------------------------------------------------------------------

BackdropHub &BackdropHub::instance() {
    static BackdropHub h;
    return h;
}

BackdropHub::BackdropHub() {
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &BackdropHub::flush);
}

void BackdropHub::add(GlassPanel *g) {
    m_glass.insert(g);
    if (!m_installed && qApp) {
        qApp->installEventFilter(this);
        m_installed = true;
    }
}

void BackdropHub::remove(GlassPanel *g) { m_glass.remove(g); }

QImage BackdropHub::grab(QWidget *window, const QRect &r, qreal dpr, QWidget *painting) {
    QImage img(QSize(qMax(1, int(std::ceil(r.width() * dpr))), qMax(1, int(std::ceil(r.height() * dpr)))), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(tk().windowTop);
    const QRect inside = r.intersected(window->rect());
    if (inside.isEmpty()) return img;
    QPainter p(&img);
    ++g_suppress;
    // Qt refuses to render a widget that is inside its own paintEvent ("Recursive repaint detected").
    // The glass widget that is painting right now contributes nothing to the backdrop anyway, so its
    // in-paint flag is cleared for the duration of the capture and restored afterwards.
    const bool wasPainting = painting && painting->testAttribute(Qt::WA_WState_InPaintEvent);
    if (wasPainting) painting->setAttribute(Qt::WA_WState_InPaintEvent, false);
    // The top left of the source region lands on targetOffset.
    window->render(&p, inside.topLeft() - r.topLeft(), QRegion(inside), QWidget::DrawWindowBackground | QWidget::DrawChildren);
    if (wasPainting) painting->setAttribute(Qt::WA_WState_InPaintEvent, true);
    --g_suppress;
    p.end();
    return img;
}

void BackdropHub::painted(GlassPanel *g) {
    auto it = m_pending.find(g->window());
    if (it == m_pending.end()) return;
    *it -= QRegion(QRect(g->mapTo(g->window(), QPoint(0, 0)), g->size()));
}

bool BackdropHub::eventFilter(QObject *o, QEvent *e) {
    if (e->type() != QEvent::Paint || g_suppress > 0 || m_glass.isEmpty()) return false;
    auto *w = qobject_cast<QWidget *>(o);
    if (!w) return false;
    for (QWidget *p = w; p; p = p->parentWidget())
        if (qobject_cast<GlassPanel *>(p)) return false;  // glass and glass content never change the backdrop
    QWidget *win = w->window();
    bool relevant = false;
    for (GlassPanel *g : std::as_const(m_glass))
        if (g->window() == win) { relevant = true; break; }
    if (!relevant) return false;
    const QRegion reg = static_cast<QPaintEvent *>(e)->region().translated(w->mapTo(win, QPoint(0, 0)));
    m_pending[win] += reg;
    if (!m_timer.isActive()) m_timer.start();
    return false;
}

void BackdropHub::flush() {
    const auto pending = m_pending;
    m_pending.clear();
    constexpr int M = 44;  // logical reach of blur + refraction
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        const QRegion &r = it.value();
        if (r.isEmpty()) continue;
        const auto glassList = m_glass.values();
        for (GlassPanel *g : glassList) {
            if (g->window() != it.key() || !g->isVisible()) continue;
            const QRect gr(g->mapTo(g->window(), QPoint(0, 0)), g->size());
            const QRegion rem = r.intersected(gr.adjusted(-M, -M, M, M)) - QRegion(gr);
            if (!rem.isEmpty()) g->update();
        }
    }
}

}  // namespace Glass

// --- GlassPanel ---------------------------------------------------------------------------------------

GlassPanel::GlassPanel(QWidget *parent, Glass::Kind kind) : QWidget(parent), m_kind(kind) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    Glass::BackdropHub::instance().add(this);
    connect(&Theme::instance(), &Theme::changed, this, [this] { m_matKey = 0; update(); });
    connect(&Glass::Settings::instance(), &Glass::Settings::levelChanged, this, [this] { m_matKey = 0; update(); });
}

GlassPanel::~GlassPanel() { Glass::BackdropHub::instance().remove(this); }

void GlassPanel::setShape(Shape s, int radius) {
    m_shape = s;
    m_radius = radius;
    m_matKey = 0;
    update();
}

void GlassPanel::setKind(Glass::Kind k) {
    m_kind = k;
    m_matKey = 0;
    update();
}

void GlassPanel::setShadowMargin(int px) {
    m_margin = qMax(0, px);
    updateGeometry();
    update();
}

QRect GlassPanel::shapeRect() const { return rect().adjusted(m_margin, m_margin, -m_margin, -m_margin); }

qreal GlassPanel::cornerRadius() const {
    const QRect r = shapeRect();
    const qreal maxR = qMin(r.width(), r.height()) / 2.0;
    return m_shape == Shape::RoundRect ? qMin<qreal>(m_radius, maxR) : maxR;
}

void GlassPanel::forceLevel(std::optional<Glass::Level> l) {
    m_forced = l;
    m_matKey = 0;
    update();
}

void GlassPanel::setBackdropOverride(const QImage &img) {
    m_backdropOverride = img;
    m_matKey = 0;
    update();
}

void GlassPanel::setBackdropDisabled(bool off) {
    m_noBackdrop = off;
    m_matKey = 0;
    update();
}

Glass::Level GlassPanel::effectiveLevel() const { return m_forced ? *m_forced : Glass::level(); }

void GlassPanel::changeEvent(QEvent *e) {
    if (e->type() == QEvent::EnabledChange || e->type() == QEvent::PaletteChange) m_matKey = 0;
    QWidget::changeEvent(e);
}

void GlassPanel::showEvent(QShowEvent *e) {
    m_matKey = 0;
    QWidget::showEvent(e);
}

void GlassPanel::resizeEvent(QResizeEvent *e) {
    m_matKey = 0;
    QWidget::resizeEvent(e);
}

QImage &GlassPanel::material(const QRect &shape, Glass::Level lvl) {
    const qreal dpr = devicePixelRatioF();
    const int w = qMax(1, int(std::lround(shape.width() * dpr))), h = qMax(1, int(std::lround(shape.height() * dpr)));
    const Glass::Material mat = Glass::Material::forKind(m_kind, tk());
    const qreal radiusDev = cornerRadius() * dpr;
    QImage crop;
    QPoint origin(0, 0);
    if (lvl != Glass::Level::Off && !m_noBackdrop) {
        const int mDev = Glass::backdropMargin(mat, lvl, dpr);
        const int mLog = int(std::ceil(mDev / dpr));
        if (!m_backdropOverride.isNull()) {
            // Test/proof path: the override is in widget-local logical coordinates, at the widget's own dpr.
            const QRect want = shape.adjusted(-mLog, -mLog, mLog, mLog);
            QImage c(QSize(int(want.width() * dpr), int(want.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
            c.setDevicePixelRatio(dpr);
            c.fill(tk().windowTop);
            QPainter p(&c);
            p.drawImage(QPointF(-want.left(), -want.top()), m_backdropOverride);
            p.end();
            crop = c;
        } else if (QWidget *win = window(); win && isVisible()) {
            const QPoint tl = mapTo(win, shape.topLeft());
            crop = Glass::BackdropHub::instance().grab(win, QRect(tl, shape.size()).adjusted(-mLog, -mLog, mLog, mLog), dpr, this);
        }
        origin = QPoint(int(std::lround(mLog * dpr)), int(std::lround(mLog * dpr)));
    }
    quint64 key = 1469598103934665603ull;
    key = Glass::mix(key, quint64(w) << 32 | quint64(h));
    key = Glass::mix(key, quint64(radiusDev * 16));
    key = Glass::mix(key, quint64(m_kind) * 7 + quint64(lvl) * 131 + (tk().dark ? 1 : 0) + quint64(dpr * 100) * 977);
    key = Glass::mix(key, crop.isNull() ? 0 : quint64(qHashBits(crop.constBits(), size_t(crop.sizeInBytes()))));
    if (key == m_matKey && !m_mat.isNull()) return m_mat;
    QElapsedTimer t;
    t.start();
    m_mat = Glass::renderMaterial(crop, origin, w, h, dpr, radiusDev, mat, lvl);
    m_matKey = key;
    m_lastMaterialMs = t.nsecsElapsed() / 1e6;
    m_recomputed = true;
    return m_mat;
}

QImage GlassPanel::materialImage() {
    m_matKey = 0;
    return material(shapeRect(), effectiveLevel());
}

void GlassPanel::drawShadow(QPainter &p, const QRect &shape, Glass::Level lvl) {
    if (m_margin <= 0) return;
    const Tokens &t = tk();
    // 0 0.5px 3px rgb(0 0 0 / 16%) (50% in dark): a few translucent rings that fade outwards.
    const qreal base = t.dark ? 0.50 : 0.16;
    const qreal r = cornerRadius();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    for (int i = 1; i <= m_margin; ++i) {
        const qreal f = 1.0 - qreal(i - 1) / m_margin;
        QColor c(0, 0, 0);
        c.setAlphaF(base * f * f * (lvl == Glass::Level::Off ? 0.7 : 0.55));
        p.setPen(QPen(c, 1.0));
        const QRectF rr = QRectF(shape).adjusted(-i + 0.5, -i + 0.5 + 0.5, i - 0.5, i - 0.5 + 0.5);
        p.drawRoundedRect(rr, r + i - 0.5, r + i - 0.5);
    }
    p.restore();
}

void GlassPanel::paintContent(QPainter &, const QRect &) {}

void GlassPanel::paintEvent(QPaintEvent *) {
    if (Glass::suppressed()) return;
    QElapsedTimer t;
    t.start();
    m_recomputed = false;
    const Glass::Level lvl = effectiveLevel();
    const QRect sr = shapeRect();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    drawShadow(p, sr, lvl);
    const QImage &mat = material(sr, lvl);
    p.drawImage(sr.topLeft(), mat);
    paintContent(p, sr);
    p.end();
    Glass::BackdropHub::instance().painted(this);
    Glass::recordStats(objectName().isEmpty() ? QString::fromLatin1(metaObject()->className()) : objectName(),
                       t.nsecsElapsed() / 1e6, m_lastMaterialMs, m_recomputed);
}
