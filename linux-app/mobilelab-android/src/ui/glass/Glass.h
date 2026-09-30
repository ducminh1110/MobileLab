#pragma once
// Liquid Glass for Qt Widgets: level switch, backdrop capture, material renderer and the GlassPanel widget.
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <optional>
#include "GlassMath.h"
#include "Theme.h"

class QSettings;
class GlassPanel;

namespace Glass {

enum class Level { Off, Blur, Full };
enum class Kind { Control, Accent, Field, Sheet };

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

// Everything needed to draw one look of the material.
struct Material {
    glass::Params params;
    QColor tintTop, tintBottom, rimLight, rimDark, sheen, opaque;
    qreal sheenHeight = 0.5;
    bool rim = true;
    static Material forKind(Kind k, const Tokens &t);
};

struct Stats {
    quint64 paints = 0;
    quint64 recomputes = 0;
    double totalMs = 0, lastMs = 0, maxMs = 0;       // full paintEvent (material lookup + draw)
    double materialTotalMs = 0, materialLastMs = 0, materialMaxMs = 0;  // material recomputation only
};
QHash<QString, Stats> allStats();
void resetStats();

// Renders the glass material for a w x h device pixel shape from `backdrop` (device pixels, larger than
// the shape by `origin` on each side). A null backdrop gives the tint + rim fallback.
QImage renderMaterial(const QImage &backdrop, QPoint origin, int w, int h, qreal dpr, qreal radiusDev,
                      const Material &m, Level level);
// Margin (device px) of backdrop needed around a shape for the given material and level.
int backdropMargin(const Material &m, Level level, qreal dpr);

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
    // Glass content painted by subclasses on top of the material.
    virtual void paintContent(QPainter &p, const QRect &shape);

protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QImage &material(const QRect &shape, Glass::Level lvl);
    void drawShadow(QPainter &p, const QRect &shape, Glass::Level lvl);
    Shape m_shape = Shape::Capsule;
    int m_radius = 12, m_margin = 3;
    Glass::Kind m_kind;
    std::optional<Glass::Level> m_forced;
    QImage m_backdropOverride;
    bool m_noBackdrop = false;
    QImage m_mat;
    quint64 m_matKey = 0;
    double m_lastMaterialMs = 0;
    bool m_recomputed = false;
};
