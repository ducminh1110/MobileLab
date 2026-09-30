#pragma once
// Pure (widget free) part of the Liquid Glass material: signed distance fields of rounded shapes,
// the edge refraction table, blur and saturate. Everything here works on QImage in device pixels
// so it can be unit tested and used from any thread.
#include <QImage>
#include <QPoint>
#include <QSize>
#include <memory>
#include <vector>

namespace glass {

// Refraction model (docs/design/liquid-glass.md):
//   displacement(d) = edge * exp(-d/edgeWidth) + rim * exp(-d/rimWidth) + base * (1 - exp(-d/baseWidth))
// where d is the depth below the shape's edge in pixels. The displacement points along the outward normal,
// so the edge of the glass shows content that lies beyond the shape, bent into it.
struct Params {
    float blur = 10.f;        // box blur radius, pixels
    float saturate = 1.6f;
    float edge = 10.f;        // px
    float edgeWidth = 5.f;    // px
    float rim = 4.f;          // px
    float rimWidth = 14.f;    // px
    float base = 0.f;         // px, faint displacement deep inside (0 keeps the centre untouched)
    float baseWidth = 30.f;   // px
    float cornerBoost = 0.35f;
    bool refract = true;
    // Maximum displacement over the shape, used to size the backdrop margin.
    float maxDisplacement() const { return (edge + rim + base) * (1.f + cornerBoost); }
    bool operator==(const Params &o) const;
};

struct Vec2 { float x = 0, y = 0; };

// Signed distance to a rounded rectangle of size w x h centred at (w/2, h/2), radius r (clamped to
// min(w,h)/2). Negative inside, zero on the edge, positive outside. x, y are pixel-centre coordinates.
float sdfRoundRect(float x, float y, float w, float h, float r);
// Outward unit normal (gradient of the SDF); (0,0) exactly at the centre of a symmetric shape.
Vec2 sdfNormal(float x, float y, float w, float h, float r);
// Anti-aliased coverage from an SDF value: 1 - smoothstep(-1, 1, sdf).
float coverage(float sdf);
// Displacement magnitude for a point `depth` pixels inside the shape.
float displacementAt(float depth, const Params &p);

class RefractionTable {
public:
    RefractionTable(int w, int h, float radius, const Params &p);
    int width() const { return m_w; }
    int height() const { return m_h; }
    float radius() const { return m_r; }
    float dx(int x, int y) const { return m_dx[size_t(y) * m_w + x]; }
    float dy(int x, int y) const { return m_dy[size_t(y) * m_w + x]; }
    float magnitude(int x, int y) const;
    quint8 mask(int x, int y) const { return m_mask[size_t(y) * m_w + x]; }
    float maxMagnitude() const { return m_max; }
    QImage maskImage() const;  // Format_Alpha8
    static std::shared_ptr<const RefractionTable> cached(int w, int h, float radius, const Params &p);
    static int cacheSize();
    static void clearCache();

private:
    int m_w, m_h;
    float m_r, m_max = 0;
    std::vector<float> m_dx, m_dy;
    std::vector<quint8> m_mask;
};

// Two-pass (horizontal + vertical) box blur, repeated `iterations` times, clamped at the edges.
// Works on Format_ARGB32_Premultiplied / RGB32; returns a new image.
QImage boxBlur(const QImage &src, int radius, int iterations = 2);
void saturate(QImage &img, float amount);
// Samples `backdrop` (already blurred and saturated, larger than the table by `origin` on every side)
// through the refraction table; the result has the table's size and is opaque (mask not applied).
// With `sharp` (a lightly blurred copy of the same backdrop) the displaced rim band blends towards the
// sharp image, so bent content stays crisp at the edge while the interior is frosted like real glass.
QImage refract(const QImage &backdrop, QPoint origin, const RefractionTable &t, bool displace, const QImage *sharp = nullptr);
// Multiplies the premultiplied pixels of `img` by the table's anti-aliased mask.
void applyMask(QImage &img, const RefractionTable &t);

}  // namespace glass
