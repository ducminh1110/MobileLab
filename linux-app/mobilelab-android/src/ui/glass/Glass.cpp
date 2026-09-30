#include "Glass.h"
#include <QApplication>
#include <QLinearGradient>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
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
    // Values follow backend/public/assets/css/glass.css (refract level): a light tint with a slight vertical gradient,
    // a 1px rim (bright top left, dim elsewhere), a tight low shadow, and only a faint top sheen.
    Material m;
    const bool dk = t.dark;
    auto W = [](int a) { return QColor(255, 255, 255, a); };
    m.tintTop = dk ? QColor(74, 78, 90, 107) : QColor(243, 246, 250, 143);
    m.tintBottom = dk ? QColor(46, 48, 56, 92) : QColor(228, 234, 241, 102);
    m.rimLight = dk ? W(87) : W(166);          // top left
    m.rimLo = dk ? W(15) : W(38);              // bottom right
    m.rimDark = dk ? QColor(0, 0, 0, 90) : QColor(30, 50, 80, 26);
    m.opaque = t.glassOpaque;
    m.lightScale = dk ? 1.0f : 0.8f;
    m.shadowAlpha = dk ? 0.62f : 0.25f;
    m.shadowSpread = 9.f;
    m.shadowOffsetY = 3.f;
    m.sheen = dk ? 0.6f : 0.5f;
    glass::Params &p = m.params;
    p.refraction = 0.42f;
    p.chroma = 0.03f;
    p.edgeHighlight = 0.f;
    p.specular = 0.10f;
    p.fresnel = 0.5f;
    p.zRadius = 7.f;
    p.blur = 8.f;
    p.edgeSharp = 0.65f;
    p.saturate = 1.5f;
    switch (k) {
    case Kind::Control:
        break;
    case Kind::Accent: {
        // A flat accent circle with a subtle top sheen.
        QColor top = t.accent.lighter(105), bottom = t.accent;
        top.setAlpha(250);
        bottom.setAlpha(250);
        m.tintTop = top;
        m.tintBottom = bottom;
        m.rimLight = W(dk ? 60 : 92);
        m.rimLo = W(0);
        m.rimDark = QColor(0, 0, 0, 0);
        p.refraction = 0.f;
        p.chroma = 0.f;
        p.specular = 0.f;
        p.zRadius = 6.f;
        p.blur = 3.f;
        m.sheen = 0.5f;
        m.opaque = t.accent;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Tabs: {
        // A flat, faintly tinted pill.
        m.tintTop = dk ? W(28) : QColor(216, 224, 233, 158);
        m.tintBottom = dk ? W(20) : QColor(208, 217, 228, 143);
        m.rimLight = dk ? W(40) : W(120);
        m.rimLo = dk ? W(10) : W(30);
        m.rimDark = dk ? W(20) : QColor(30, 50, 80, 15);
        m.lightScale = dk ? 0.4f : 0.3f;
        p.refraction = 0.12f;
        p.chroma = 0.f;
        p.specular = 0.03f;
        p.zRadius = 8.f;
        p.blur = 6.f;
        p.saturate = 1.2f;
        m.sheen = 0.15f;
        m.opaque = t.field;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Field: {
        m.tintTop = dk ? W(26) : QColor(232, 235, 239, 199);
        m.tintBottom = dk ? W(18) : QColor(238, 240, 244, 178);
        m.rimLight = dk ? W(30) : W(80);
        m.rimLo = dk ? W(8) : W(20);
        m.rimDark = dk ? W(24) : QColor(30, 50, 80, 31);
        m.lightScale = dk ? 0.4f : 0.3f;
        p.refraction = 0.10f;
        p.chroma = 0.f;
        p.specular = 0.02f;
        p.zRadius = 8.f;
        p.blur = 6.f;
        p.saturate = 1.2f;
        m.sheen = 0.f;
        m.opaque = t.field;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Quiet: {
        // Almost clear, rim only.
        m.tintTop = dk ? W(15) : QColor(246, 248, 251, 87);
        m.tintBottom = dk ? W(8) : QColor(236, 240, 245, 56);
        m.rimLight = dk ? W(46) : W(115);
        m.rimLo = dk ? W(10) : W(26);
        m.rimDark = dk ? W(20) : QColor(30, 50, 80, 18);
        m.lightScale = dk ? 0.5f : 0.35f;
        p.refraction = 0.12f;
        p.chroma = 0.f;
        p.specular = 0.02f;
        p.zRadius = 7.f;
        p.blur = 5.f;
        p.saturate = 1.15f;
        m.sheen = 0.2f;
        m.blurTintBoost = 1.15f;
        m.opaque = t.field;
        m.shadowAlpha = 0.f;
        break;
    }
    case Kind::Sheet: {
        m.tintTop = dk ? QColor(54, 56, 64, 224) : QColor(250, 251, 253, 230);
        m.tintBottom = dk ? QColor(46, 48, 56, 214) : QColor(244, 246, 250, 219);
        m.rimLight = dk ? W(64) : W(150);
        m.rimLo = dk ? W(14) : W(40);
        m.rimDark = dk ? QColor(0, 0, 0, 100) : QColor(30, 50, 80, 30);
        p.refraction = 0.05f;
        p.chroma = 0.f;
        p.specular = 0.04f;
        p.zRadius = 10.f;
        p.blur = 9.f;
        p.edgeSharp = 0.15f;
        m.sheen = 0.25f;
        m.blurTintBoost = 1.f;
        m.opaque = dk ? QColor("#2c2c31") : QColor("#f7f7f9");
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
    m.sheen *= 0.5f;
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
        const float boost = level == Level::Blur ? m.blurTintBoost : 1.f;
        auto boosted = [&](QColor c) { c.setAlphaF(std::min<qreal>(1.0, c.alphaF() * boost)); return c; };
        QLinearGradient tint(0, 0, 0, h);
        tint.setColorAt(0, boosted(m.tintTop));
        tint.setColorAt(1, boosted(m.tintBottom));
        pa.fillRect(QRect(0, 0, w, h), tint);
    }
    const qreal wpx = std::max<qreal>(1.0, std::round(dpr));
    const qreal rad = r.table->radius();
    if (level != Level::Off && m.sheen > 0.f) {
        // A faint highlight hugging the top edge (soft radial at the top left, plus a thin top fade).
        const float sa = std::min(1.f, m.sheen);
        QRadialGradient rg(QPointF(w * 0.18, -h * 0.2), std::max<qreal>(w * 0.55, h * 0.9));
        rg.setColorAt(0, QColor(255, 255, 255, int(0.30f * sa * 255)));
        rg.setColorAt(1, QColor(255, 255, 255, 0));
        pa.setClipPath([&] { QPainterPath cp; cp.addRoundedRect(QRectF(0, 0, w, h), rad, rad); return cp; }());
        pa.fillRect(QRect(0, 0, w, h), rg);
        QLinearGradient lg(0, 0, 0, h * 0.4);
        lg.setColorAt(0, QColor(255, 255, 255, int(0.14f * sa * 255)));
        lg.setColorAt(1, QColor(255, 255, 255, 0));
        pa.fillRect(QRect(0, 0, w, int(h * 0.4) + 1), lg);
        pa.setClipping(false);
    }
    // One 1px border: the hairline edge colour underneath, the rim light over it (bright top left, dim elsewhere).
    pa.setBrush(Qt::NoBrush);
    const QRectF edgeR(wpx / 2, wpx / 2, w - wpx, h - wpx);
    const qreal er = std::max<qreal>(0.0, rad - wpx / 2);
    if (level == Level::Off) {
        QColor c = m.rimDark;
        c.setAlpha(std::max(c.alpha(), 46));
        pa.setPen(QPen(c, wpx));
        pa.drawRoundedRect(edgeR, er, er);
    } else {
        if (m.rimDark.alpha() > 0) {
            pa.setPen(QPen(m.rimDark, wpx));
            pa.drawRoundedRect(edgeR, er, er);
        }
        QLinearGradient rim(0, 0, w * 0.55, h * 1.0);   // about 160 degrees like the web rim
        rim.setColorAt(0, m.rimLight);
        rim.setColorAt(0.42, m.rimLo);
        rim.setColorAt(0.68, m.rimLo);
        rim.setColorAt(1, QColor(m.rimLight.red(), m.rimLight.green(), m.rimLight.blue(), m.rimLight.alpha() / 2));
        pa.setPen(QPen(QBrush(rim), wpx));
        pa.drawRoundedRect(edgeR, er, er);
    }
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
        // The blur / offset are fitted so the falloff ends inside the widget's shadow margin (no hard cut).
        const float fit = std::min(1.f, std::max(0.f, (float(md) - 0.5f * float(dpr))) / ((mat.shadowSpread * 0.67f + mat.shadowOffsetY) * float(dpr)));
        const QImage a = glass::shadowAlpha(w, h, float(radiusDev), md, mat.shadowSpread * fit * float(dpr), mat.shadowOffsetY * fit * float(dpr));
        m_shadow = QImage(a.size(), QImage::Format_ARGB32_Premultiplied);
        const float k = mat.shadowAlpha * (lvl == Glass::Level::Off ? 0.7f : 1.f);
        const QColor base = tk().dark ? QColor(0, 0, 0) : QColor(20, 40, 70);
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
