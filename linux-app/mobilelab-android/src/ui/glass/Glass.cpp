#include "Glass.h"
#include <QApplication>
#include <QLinearGradient>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSettings>
#include <cmath>
#include "UiUtil.h"

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
    m.opaque = t.glassOpaque;
    m.lightScale = t.dark ? 1.0f : 0.8f;
    m.shadowAlpha = t.dark ? 0.55f : 0.22f;
    glass::Params &p = m.params;
    p.refraction = 0.42f;
    p.chroma = 0.04f;
    p.edgeHighlight = 0.30f;
    p.specular = 0.28f;
    p.fresnel = 1.0f;
    p.zRadius = 14.f;
    p.blur = 6.f;
    p.edgeSharp = 0.65f;
    p.saturate = 1.5f;
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
        p.refraction = 0.10f;
        p.chroma = 0.f;
        p.specular = 0.22f;
        p.edgeHighlight = 0.12f;
        p.zRadius = 12.f;
        p.blur = 3.f;
        m.opaque = t.accent;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Field: {
        // Flat grey glass (navigator tab bar, filter bars, jump bar controls): a faint dark veil in light mode,
        // a faint light veil in dark mode, hardly any highlight.
        m.tintTop = t.dark ? QColor(255, 255, 255, 22) : QColor(30, 50, 80, 22);
        m.tintBottom = t.dark ? QColor(255, 255, 255, 16) : QColor(30, 50, 80, 30);
        m.rimLight = QColor(255, 255, 255, t.dark ? 26 : 70);
        m.rimDark = QColor(0, 0, 0, t.dark ? 70 : 22);
        m.lightScale = t.dark ? 0.5f : 0.35f;
        p.refraction = 0.18f;
        p.chroma = 0.f;
        p.specular = 0.06f;
        p.edgeHighlight = 0.06f;
        p.zRadius = 10.f;
        m.opaque = t.field;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Sheet: {
        m.tintTop = t.dark ? QColor(48, 48, 54, 226) : QColor(250, 250, 252, 222);
        m.tintBottom = t.dark ? QColor(34, 34, 40, 232) : QColor(244, 246, 250, 232);
        p.refraction = 0.06f;
        p.chroma = 0.f;
        p.specular = 0.10f;
        p.zRadius = 18.f;
        p.blur = 9.f;
        p.edgeSharp = 0.15f;
        m.opaque = t.dark ? QColor("#2c2c31") : QColor("#f7f7f9");
        break;
    }
    }
    return m;
}

Material Material::pressed() const {
    Material m = *this;
    m.params.zRadius = std::max(3.f, params.zRadius * 0.45f);
    m.params.specular *= 0.4f;
    m.params.refraction *= 0.6f;
    m.lightScale *= 0.7f;
    m.tintTop = QColor::fromRgbF(tintTop.redF() * 0.92, tintTop.greenF() * 0.92, tintTop.blueF() * 0.92, std::min(1.0, tintTop.alphaF() + 0.06));
    m.tintBottom = QColor::fromRgbF(tintBottom.redF() * 0.92, tintBottom.greenF() * 0.92, tintBottom.blueF() * 0.92, std::min(1.0, tintBottom.alphaF() + 0.06));
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

glass::Params deviceParams(const Material &m, qreal dpr, Level level) {
    Q_UNUSED(level);
    glass::Params p = m.params;
    p.scale = float(dpr);
    p.zRadius *= float(dpr);
    if (p.maxOffset > 0) p.maxOffset *= float(dpr);
    p.blur = std::max(1.f, std::round(p.blur * float(dpr)));
    return p;
}

int backdropMargin(const glass::ShapeTable &t, const glass::Params &p, Level level) {
    const float reach = level == Level::Full ? t.maxReach() : 0.f;
    return int(std::ceil(reach)) + 2 * int(p.blur) + 3;
}

int marginFor(const Material &m, qreal dpr, Level level, int w, int h, qreal radiusDev) {
    const glass::Params p = deviceParams(m, dpr, level);
    return backdropMargin(*glass::ShapeTable::cached(w, h, float(radiusDev), p), p, level);
}

QImage Rendered::lit(float lightScale, bool light) const {
    QImage img = pre;
    img.detach();
    glass::applyLightAndMask(img, *table, lightScale, light);
    return img;
}

Rendered renderMaterialStages(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev,
                              const Material &m, Level level) {
    const glass::Params p = deviceParams(m, dpr, level);
    Rendered r;
    r.table = glass::ShapeTable::cached(w, h, float(radiusDev), p);
    if (level == Level::Off || backdrop.isNull()) {
        // Off: opaque control. No backdrop: tint + rim fallback, opaque enough to keep text contrast.
        r.pre = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        r.pre.fill(m.opaque.rgb() | 0xff000000u);
    } else {
        QImage soft = glass::boxBlur(backdrop, int(p.blur), 2);
        glass::saturate(soft, p.saturate);
        glass::SampleOptions so;
        so.displace = level == Level::Full;
        so.edgeSharp = p.edgeSharp;
        if (level == Level::Full) {
            QImage sharp = backdrop;
            sharp.detach();
            sharp = sharp.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            glass::saturate(sharp, p.saturate);
            r.pre = glass::sampleThrough(soft, &sharp, origin, *r.table, so);
        } else {
            r.pre = glass::sampleThrough(soft, nullptr, origin, *r.table, so);
        }
    }
    QPainter pa(&r.pre);
    pa.setRenderHint(QPainter::Antialiasing);
    if (level != Level::Off && !backdrop.isNull()) {
        QLinearGradient tint(0, 0, 0, h);
        tint.setColorAt(0, m.tintTop);
        tint.setColorAt(1, m.tintBottom);
        pa.fillRect(QRect(0, 0, w, h), tint);
    }
    // Rim: brighter at the top left (light source), dimmer bottom right; on Off a plain hairline border.
    const qreal wpx = std::max<qreal>(1.0, dpr);
    QLinearGradient rim(0, 0, w, h);
    rim.setColorAt(0, m.rimLight);
    rim.setColorAt(0.5, level == Level::Off ? m.rimDark : QColor(m.rimLight.red(), m.rimLight.green(), m.rimLight.blue(), m.rimLight.alpha() / 3));
    rim.setColorAt(1, m.rimDark);
    pa.setPen(QPen(QBrush(rim), wpx));
    pa.setBrush(Qt::NoBrush);
    pa.drawRoundedRect(QRectF(wpx / 2, wpx / 2, w - wpx, h - wpx), r.table->radius() - wpx / 2, r.table->radius() - wpx / 2);
    pa.end();
    return r;
}

QImage renderMaterial(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev,
                      const Material &m, Level level) {
    Rendered r = renderMaterialStages(backdrop, origin, w, h, dpr, radiusDev, m, level);
    QImage img = r.lit(m.lightScale, level != Level::Off);
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
    auto reset = [this] { m_normal = {}; m_flat = {}; m_shadowKey = 0; update(); };
    connect(&Theme::instance(), &Theme::changed, this, reset);
    connect(&Glass::Settings::instance(), &Glass::Settings::levelChanged, this, reset);
}

GlassPanel::~GlassPanel() { Glass::BackdropHub::instance().remove(this); }

void GlassPanel::setShape(Shape s, int radius) {
    m_shape = s;
    m_radius = radius;
    m_normal = {};
    m_flat = {};
    update();
}

void GlassPanel::setKind(Glass::Kind k) {
    m_kind = k;
    m_normal = {};
    m_flat = {};
    update();
}

void GlassPanel::setShadowMargin(int px) {
    m_margin = qMax(0, px);
    m_shadowKey = 0;
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
    m_normal = {};
    m_flat = {};
    update();
}

void GlassPanel::setBackdropOverride(const QImage &img) {
    m_backdropOverride = img;
    m_normal = {};
    m_flat = {};
    update();
}

void GlassPanel::setBackdropDisabled(bool off) {
    m_noBackdrop = off;
    m_normal = {};
    m_flat = {};
    update();
}

Glass::Level GlassPanel::effectiveLevel() const { return m_forced ? *m_forced : Glass::level(); }

void GlassPanel::changeEvent(QEvent *e) {
    if (e->type() == QEvent::EnabledChange) { m_normal = {}; m_flat = {}; }
    QWidget::changeEvent(e);
}

void GlassPanel::showEvent(QShowEvent *e) {
    m_normal.key = 0;
    m_flat.key = 0;
    QWidget::showEvent(e);
}

void GlassPanel::resizeEvent(QResizeEvent *e) {
    m_normal.key = 0;
    m_flat.key = 0;
    m_shadowKey = 0;
    QWidget::resizeEvent(e);
}

void GlassPanel::setHoverLook(bool on) {
    if (m_hover == on) return;
    m_hover = on;
    update();
}

void GlassPanel::setPressedLook(bool on) {
    if (m_pressedLook == on) return;
    m_pressedLook = on;
    if (m_pressAnim) { m_pressAnim->stop(); m_pressAnim->deleteLater(); m_pressAnim = nullptr; }
    m_pressAnim = Ui::animate(this, m_pressT, on ? 1.0 : 0.0, 120, [this](qreal v) { m_pressT = v; update(); },
                              QEasingCurve::OutCubic, [this, on] { m_pressAnim = nullptr; m_pressT = on ? 1.0 : 0.0; update(); });
}

void GlassPanel::ensureVariant(Variant &v, bool pressed, const QRect &shape, Glass::Level lvl) {
    const qreal dpr = devicePixelRatioF();
    const int w = qMax(1, int(std::lround(shape.width() * dpr))), h = qMax(1, int(std::lround(shape.height() * dpr)));
    Glass::Material mat = Glass::Material::forKind(m_kind, tk());
    if (pressed) mat = mat.pressed();
    const qreal radiusDev = cornerRadius() * dpr;
    const glass::Params dp = Glass::deviceParams(mat, dpr, lvl);
    auto table = glass::ShapeTable::cached(w, h, float(radiusDev), dp);
    QImage crop;
    QPoint origin(0, 0);
    if (lvl != Glass::Level::Off && !m_noBackdrop) {
        const int mDev = Glass::backdropMargin(*table, dp, lvl);
        const int mLog = int(std::ceil(mDev / dpr));
        if (!m_backdropOverride.isNull()) {
            // Test/proof path: the override is in widget-local logical coordinates, at the widget's own dpr.
            const QRect want = shape.adjusted(-mLog, -mLog, mLog, mLog);
            QImage c(QSize(int(std::ceil(want.width() * dpr)), int(std::ceil(want.height() * dpr))), QImage::Format_ARGB32_Premultiplied);
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
    key = Glass::mix(key, quint64(m_kind) * 7 + quint64(lvl) * 131 + (tk().dark ? 1 : 0) + quint64(dpr * 100) * 977 + (pressed ? 55555 : 0));
    key = Glass::mix(key, crop.isNull() ? 0 : quint64(qHashBits(crop.constBits(), size_t(crop.sizeInBytes()))));
    if (key == v.key && v.r.table) return;
    QElapsedTimer t;
    t.start();
    v.r = Glass::renderMaterialStages(crop, origin, w, h, dpr, radiusDev, mat, lvl);
    v.key = key;
    v.finState = -1;
    m_lastMaterialMs = t.nsecsElapsed() / 1e6;
    m_recomputed = true;
}

const QImage &GlassPanel::finalImage(Variant &v, Glass::Level lvl) {
    const int state = (m_hover ? 1 : 0) + 2 * int(lvl);
    if (state != v.finState || v.fin.isNull()) {
        const Glass::Material mat = Glass::Material::forKind(m_kind, tk());
        v.fin = v.r.lit(mat.lightScale * (m_hover ? 1.35f : 1.f), lvl != Glass::Level::Off);
        v.fin.setDevicePixelRatio(devicePixelRatioF());
        v.finState = state;
    }
    return v.fin;
}

QImage GlassPanel::materialImage() {
    m_normal = {};
    const Glass::Level lvl = effectiveLevel();
    ensureVariant(m_normal, false, shapeRect(), lvl);
    return finalImage(m_normal, lvl);
}

void GlassPanel::drawShadow(QPainter &p, const QRect &shape, Glass::Level lvl) {
    if (m_margin <= 0) return;
    const Glass::Material mat = Glass::Material::forKind(m_kind, tk());
    if (mat.shadowAlpha <= 0.f) return;
    const qreal dpr = devicePixelRatioF();
    const int w = qMax(1, int(std::lround(shape.width() * dpr))), h = qMax(1, int(std::lround(shape.height() * dpr)));
    const int md = int(std::lround(m_margin * dpr));
    const qreal radiusDev = cornerRadius() * dpr;
    quint64 key = Glass::mix(Glass::mix(quint64(w) << 32 | quint64(h), quint64(md)), quint64(radiusDev * 16) + (tk().dark ? 1 : 0) + quint64(lvl) * 3 + quint64(dpr * 100) * 7);
    if (key != m_shadowKey || m_shadow.isNull()) {
        const QImage a = glass::shadowAlpha(w, h, float(radiusDev), md, mat.shadowSpread * float(dpr) * 0.65f, mat.shadowOffsetY * float(dpr));
        m_shadow = QImage(a.size(), QImage::Format_ARGB32_Premultiplied);
        const float k = mat.shadowAlpha * (lvl == Glass::Level::Off ? 0.7f : 1.f);
        const QColor base = tk().dark ? QColor(0, 0, 0) : QColor(20, 30, 60);
        for (int y = 0; y < a.height(); ++y) {
            const uchar *src = a.constScanLine(y);
            quint32 *dst = reinterpret_cast<quint32 *>(m_shadow.scanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                const float al = src[x] / 255.f * k;
                dst[x] = (quint32(al * 255 + 0.5f) << 24) | (quint32(base.red() * al + 0.5f) << 16) | (quint32(base.green() * al + 0.5f) << 8) | quint32(base.blue() * al + 0.5f);
            }
        }
        m_shadow.setDevicePixelRatio(dpr);
        m_shadowKey = key;
    }
    p.drawImage(QPointF(shape.left() - m_margin, shape.top() - m_margin), m_shadow);
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
    ensureVariant(m_normal, false, sr, lvl);
    p.drawImage(sr.topLeft(), finalImage(m_normal, lvl));
    if (m_pressT > 0.001) {
        ensureVariant(m_flat, true, sr, lvl);
        p.setOpacity(m_pressT);
        p.drawImage(sr.topLeft(), finalImage(m_flat, lvl));
        p.setOpacity(1.0);
    }
    paintContent(p, sr);
    p.end();
    Glass::BackdropHub::instance().painted(this);
    Glass::recordStats(objectName().isEmpty() ? QString::fromLatin1(metaObject()->className()) : objectName(),
                       t.nsecsElapsed() / 1e6, m_lastMaterialMs, m_recomputed);
}
