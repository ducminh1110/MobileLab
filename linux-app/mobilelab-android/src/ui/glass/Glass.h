#pragma once
// Liquid Glass for Qt Widgets: level switch, backdrop capture, material renderer and the GlassPanel widget.
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>
#include <optional>
#include "GlassMath.h"
#include "Theme.h"

class QSettings;
class GlassPanel;

namespace Glass {

enum class Level { Off, Blur, Full };
// Control: toolbar groups, capsule, pills. Accent: selected tab. Tabs: navigator/inspector tab bar. Field: filter bars.
// Quiet: jump bar and canvas bar controls (almost clear, rim only). Sheet: menus, dialogs, toast.
enum class Kind { Control, Accent, Field, Sheet, Tabs, Quiet };

Level parseLevel(const QString &s, Level fallback);
QString levelName(Level l);

// Global switch: MOBILELAB_GLASS=off|blur|full (default full), Settings > Liquid Glass, and
// MOBILELAB_REDUCE_TRANSPARENCY=1 which forces Off (mirrors macOS "Reduce transparency").
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();
    Level level() const { return m_level; }
    void setLevel(Level l);
    bool envOverride() const { return m_envOverride; }
    void load(QSettings &s);
    void save(QSettings &s) const;
signals:
    void levelChanged();
private:
    Settings();
    Level m_level = Level::Full;
    bool m_envOverride = false;
};
inline Level level() { return Settings::instance().level(); }

// True while a backdrop is being captured: glass widgets (and their content) must not paint then.
bool suppressed();

// Everything needed to draw one look of the material (logical units; scaled by the device pixel ratio).
struct Material {
    glass::Params params;
    // rimLight/rimLo: the 1px rim gradient (bright top left, dim elsewhere); rimDark: the hairline edge under it
    // (one pixel, same as the rim, so there is a single border, not two).
    QColor tintTop, tintBottom, rimLight, rimLo, rimDark, opaque;
    bool sheenInFallback = false; // draw the sheen even without a backdrop (accent circle)
    float sheen = 0.f;           // faint highlight hugging the top edge, 0..1
    float blurTintBoost = 1.28f; // Blur level (no refraction) uses a more opaque tint
    float lightScale = 1.f;
    float shadowSpread = 9.f;    // px, blur of the tight shadow layer (fitted down to the widget's shadow margin)
    float shadowOffsetY = 3.f;   // px
    float shadowAlpha = 0.25f;   // strength multiplier of the normalised shadow image (0 = none)
    static Material forKind(Kind k, const Tokens &t);
    // The look while pressed: bevel and specular flatten.
    Material pressed() const;
};

struct Stats {
    quint64 paints = 0;
    quint64 recomputes = 0;
    double totalMs = 0, lastMs = 0, maxMs = 0;                              // full paintEvent
    double materialTotalMs = 0, materialLastMs = 0, materialMaxMs = 0;      // material recomputation only
};
QHash<QString, Stats> allStats();
void resetStats();

// Everything a render produces before the light pass, so hover / press can re-light without resampling.
struct Rendered {
    QImage pre;                                  // sampled + tinted + rim, opaque, before light and mask
    std::shared_ptr<const glass::ShapeTable> table;
    QImage lit(float lightScale, bool light) const;   // copy with the light layers added and the mask applied
};
// Renders the glass material for a w x h device pixel shape from `backdrop` (device pixels, larger than the
// shape by `origin` on each side). A null backdrop gives the tint + rim fallback (opaque).
Rendered renderMaterialStages(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev,
                              const Material &m, Level level);
// Convenience: stages + light + mask in one go.
QImage renderMaterial(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev,
                      const Material &m, Level level);
// Params scaled to device pixels.
glass::Params deviceParams(const Material &m, qreal dpr, Level level);
// Margin (device px) of backdrop needed around a shape for the given table and level.
int backdropMargin(const glass::ShapeTable &t, const glass::Params &p, Level level);
int marginFor(const Material &m, qreal dpr, Level level, int w, int h, qreal radiusDev);

// Captures what is behind glass widgets and repaints them when it changes.
class BackdropHub : public QObject {
    Q_OBJECT
public:
    static BackdropHub &instance();
    void add(GlassPanel *g);
    void remove(GlassPanel *g);
    // Renders `logicalRect` (window coordinates) of `window` without glass into a new image.
    // `painting` is the glass widget whose paintEvent is running (if any).
    QImage grab(QWidget *window, const QRect &logicalRect, qreal dpr, QWidget *painting = nullptr);
    void painted(GlassPanel *g);
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    BackdropHub();
    void flush();
    QSet<GlassPanel *> m_glass;
    QHash<QWidget *, QRegion> m_pending;
    QTimer m_timer;
    bool m_installed = false;
};

}  // namespace Glass

class GlassPanel : public QWidget {
    Q_OBJECT
public:
    enum class Shape { Capsule, Circle, RoundRect };
    explicit GlassPanel(QWidget *parent = nullptr, Glass::Kind kind = Glass::Kind::Control);
    ~GlassPanel() override;
    void setShape(Shape s, int radius = 12);
    void setKind(Glass::Kind k);
    Glass::Kind kind() const { return m_kind; }
    // The widget rect is the shape plus this margin on every side, which the drop shadow needs.
    void setShadowMargin(int px);
    int shadowMargin() const { return m_margin; }
    QRect shapeRect() const;
    qreal cornerRadius() const;
    void forceLevel(std::optional<Glass::Level> l);
    // Used by tests and the proof tool: bypass the window capture and use this image as backdrop.
    void setBackdropOverride(const QImage &img);
    // Simulates a window without a usable backdrop: the tint + rim fallback is drawn.
    void setBackdropDisabled(bool off);
    Glass::Level effectiveLevel() const;
    QImage materialImage();  // renders the current material (for tests)
    // Interaction: hover lifts the light slightly, press flattens the bevel (about 120 ms cross fade).
    void setHoverLook(bool on);
    void setPressedLook(bool on);
    // Glass content painted by subclasses on top of the material.
    virtual void paintContent(QPainter &p, const QRect &shape);

protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    struct Variant {
        Glass::Rendered r;
        quint64 key = 0;
        QImage fin;
        int finState = -1;
    };
    void ensureVariant(Variant &v, bool pressed, const QRect &shape, Glass::Level lvl);
    const QImage &finalImage(Variant &v, Glass::Level lvl);
    void drawShadow(QPainter &p, const QRect &shape, Glass::Level lvl);
    Shape m_shape = Shape::Capsule;
    int m_radius = 12, m_margin = 3;
    Glass::Kind m_kind;
    std::optional<Glass::Level> m_forced;
    QImage m_backdropOverride;
    bool m_noBackdrop = false, m_hover = false, m_pressedLook = false;
    qreal m_pressT = 0;
    QVariantAnimation *m_pressAnim = nullptr;
    Variant m_normal, m_flat;
    QImage m_shadow;
    quint64 m_shadowKey = 0;
    double m_lastMaterialMs = 0;
    bool m_recomputed = false;
};
