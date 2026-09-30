#pragma once
// Pure (widget free) part of the Liquid Glass material. Our own implementation of the physical model in
// docs/design/liquid-glass.md ("Second reference"): a bevel height field on a rounded-rect SDF, dual surface
// refraction, chromatic aberration, edge weighted blur, Fresnel and Blinn-Phong light layers, an inner stroke
// and a two-part shadow. Everything that depends only on the shape is precomputed once per (size, radius,
// parameters) in a ShapeTable shared by all widgets of that size, so a repaint is: sample the backdrop through
// the table, tint, add the precomputed light, mask.
#include <QImage>
#include <QPoint>
#include <QSize>
#include <memory>
#include <vector>

namespace glass {

struct Params {
    // Physical model. Lengths are in *device* pixels (the widget multiplies by its device pixel ratio).
    float refraction = 0.45f;   // strength; 0.69 in the reference, about 30 px at full
    float chroma = 0.05f;       // chromatic aberration amount
    float edgeHighlight = 0.30f;
    float specular = 0.40f;
    float fresnel = 1.0f;
    float zRadius = 14.f;       // bevel depth; clamped to half the short side
    float ior = 1.5f;
    float maxOffset = 0.f;      // soft cap of the refraction offset in px, 0 = zRadius
    float scale = 1.f;          // device pixel ratio: scales the fixed pixel constants of the model
    // Frosting (used by the renderer, not by the table).
    float blur = 5.f;           // box blur radius, px
    float edgeSharp = 0.6f;     // how much the rim blends towards the unblurred backdrop
    float saturate = 1.4f;
    bool operator==(const Params &o) const;
    // Everything the table depends on (blur / saturate / edgeSharp are applied when rendering).
    bool sameShape(const Params &o) const;
};

struct Vec2 { float x = 0, y = 0; };

// Signed distance to a rounded rectangle of size w x h (radius clamped to min(w,h)/2). Negative inside.
// x, y are pixel-centre coordinates relative to the rectangle's top left.
float sdfRoundRect(float x, float y, float w, float h, float r);
// Outward unit normal (gradient of the SDF); (0,0) on the medial axis.
Vec2 sdfNormal(float x, float y, float w, float h, float r);
// Anti-aliased coverage from an SDF value: 1 - smoothstep(-1, 1, sdf).
float coverage(float sdf);
// Bevel height at depth d inside the edge: sqrt(d (2 zR - d)) for d < zR, zR beyond, 0 outside.
float bevelHeight(float depth, float zR);
// dh/dd of the bevel, analytic (infinite at the very edge, so depth is clamped to half a pixel).
float bevelSlope(float depth, float zR);

// Per pixel, shape only.
struct Texel {
    qint16 rx = 0, ry = 0;      // refraction offset, 1/16 px, points into the shape
    qint16 cx = 0, cy = 0;      // chromatic aberration vector, 1/16 px (red samples +c, blue -c)
    quint8 edge = 0;            // 0 deep inside .. 255 at the edge
    quint8 add = 0;             // additive white light: specular + rim + inner glow + stroke + reflection (0..1)
    quint8 wmix = 0;            // Fresnel mix towards white, 0..0.2 stored as 0..255
    quint8 depth = 0;           // smoothstep(0, zR, inside)
    quint8 mask = 0;            // anti-aliased shape coverage
};

class ShapeTable {
public:
    ShapeTable(int w, int h, float radius, const Params &p);
    int width() const { return m_w; }
    int height() const { return m_h; }
    float radius() const { return m_r; }
    float zRadius() const { return m_zR; }
    const Params &params() const { return m_p; }
    const Texel &at(int x, int y) const { return m_t[size_t(y) * m_w + x]; }
    // Convenience accessors (pixels / 0..1).
    float dx(int x, int y) const { return at(x, y).rx / 16.f; }
    float dy(int x, int y) const { return at(x, y).ry / 16.f; }
    float cx(int x, int y) const { return at(x, y).cx / 16.f; }
    float cy(int x, int y) const { return at(x, y).cy / 16.f; }
    float magnitude(int x, int y) const;
    float light(int x, int y) const { return at(x, y).add / 255.f; }
    quint8 mask(int x, int y) const { return at(x, y).mask; }
    float maxMagnitude() const { return m_max; }
    // Largest displacement of any channel sample (refraction + chroma), for sizing the backdrop margin.
    float maxReach() const { return m_reach; }
    QImage maskImage() const;  // Format_Alpha8
    size_t bytes() const { return m_t.size() * sizeof(Texel); }
    static std::shared_ptr<const ShapeTable> cached(int w, int h, float radius, const Params &p);
    static int cacheSize();
    static void clearCache();

private:
    int m_w, m_h;
    float m_r, m_zR, m_max = 0, m_reach = 0;
    Params m_p;
    std::vector<Texel> m_t;
};

// Drop shadow alpha (Alpha8) of a w x h shape, drawn in an image padded by `margin` px on every side:
// a wide Gaussian-like falloff (weight 0.65) plus a tight contact shadow (0.35), offset down by offsetY.
// Cached per (size, radius, margin, spread, offset).
QImage shadowAlpha(int w, int h, float radius, int margin, float spread, float offsetY);

QImage boxBlur(const QImage &src, int radius, int iterations = 2);
void saturate(QImage &img, float amount);

struct SampleOptions {
    bool displace = true;       // refraction + chromatic aberration (Full level)
    float edgeSharp = 0.6f;     // rim weight of the sharp backdrop
};
// Samples the backdrop through the table. `soft` is the blurred + saturated backdrop, `sharp` the unblurred one
// (may be null); both are larger than the table by `origin` on every side. Opaque result, table size.
QImage sampleThrough(const QImage &soft, const QImage *sharp, QPoint origin, const ShapeTable &t, const SampleOptions &o);
// Adds the precomputed light layers (scaled by `lightScale`) and multiplies by the anti-aliased mask.
void applyLightAndMask(QImage &img, const ShapeTable &t, float lightScale, bool light = true);

}  // namespace glass
