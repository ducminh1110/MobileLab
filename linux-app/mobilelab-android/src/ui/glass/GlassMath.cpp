#include "GlassMath.h"
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <array>
#include <cmath>
#include <list>

namespace glass {

namespace {
float smooth(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.f : 1.f;
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}
float clampRadius(float w, float h, float r) { return std::max(0.f, std::min(r, std::min(w, h) * 0.5f)); }
qint16 q16(float v) { return qint16(std::lround(std::max(-2000.f, std::min(2000.f, v)) * 16.f)); }
quint8 q8(float v) { return quint8(std::lround(std::max(0.f, std::min(1.f, v)) * 255.f)); }
}

bool Params::operator==(const Params &o) const { return sameShape(o) && blur == o.blur && saturate == o.saturate && edgeSharp == o.edgeSharp; }

bool Params::sameShape(const Params &o) const {
    return refraction == o.refraction && chroma == o.chroma && edgeHighlight == o.edgeHighlight && specular == o.specular &&
           fresnel == o.fresnel && zRadius == o.zRadius && ior == o.ior && maxOffset == o.maxOffset && scale == o.scale;
}

float sdfRoundRect(float x, float y, float w, float h, float r) {
    r = clampRadius(w, h, r);
    const float px = x - w * 0.5f, py = y - h * 0.5f;
    const float qx = std::fabs(px) - (w * 0.5f - r), qy = std::fabs(py) - (h * 0.5f - r);
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

Vec2 sdfNormal(float x, float y, float w, float h, float r) {
    r = clampRadius(w, h, r);
    const float px = x - w * 0.5f, py = y - h * 0.5f;
    const float sx = px < 0 ? -1.f : 1.f, sy = py < 0 ? -1.f : 1.f;
    const float qx = std::fabs(px) - (w * 0.5f - r), qy = std::fabs(py) - (h * 0.5f - r);
    if (qx > 0 && qy > 0) {
        const float l = std::sqrt(qx * qx + qy * qy);
        return {qx / l * sx, qy / l * sy};
    }
    if (qx > qy) return {sx, 0.f};
    if (qy > qx) return {0.f, sy};
    return {0.f, 0.f};
}

float coverage(float sdf) { return 1.f - smooth(-1.f, 1.f, sdf); }

float bevelHeight(float d, float zR) {
    if (d <= 0.f) return 0.f;
    if (d >= zR) return zR;
    return std::sqrt(d * (2.f * zR - d));
}

float bevelSlope(float d, float zR) {
    if (d >= zR || zR <= 0.f) return 0.f;
    d = std::max(d, 0.5f);
    return (zR - d) / std::sqrt(d * (2.f * zR - d));
}

// --- ShapeTable -----------------------------------------------------------------------------------------

ShapeTable::ShapeTable(int w, int h, float radius, const Params &p)
    : m_w(std::max(1, w)), m_h(std::max(1, h)), m_r(clampRadius(float(w), float(h), radius)), m_p(p), m_t(size_t(m_w) * m_h) {
    const float fw = float(m_w), fh = float(m_h);
    const float halfX = fw * 0.5f, halfY = fh * 0.5f, maxD = std::min(halfX, halfY);
    m_zR = std::max(1.f, std::min(p.zRadius, maxD));
    const float zR = m_zR;
    const float k = 1.f - 1.f / std::max(1.01f, p.ior);
    const float capPx = p.maxOffset > 0 ? p.maxOffset : zR;
    const float S = std::max(0.5f, p.scale);
    // Light directions (x right, y up, z towards the viewer), as in the reference lighting rig.
    auto norm3 = [](float x, float y, float z) { const float l = std::sqrt(x * x + y * y + z * z); return std::array<float, 3>{x / l, y / l, z / l}; };
    auto half3 = [&](const std::array<float, 3> &l) { return norm3(l[0], l[1], l[2] + 1.f); };
    const auto L1 = norm3(0.4f, 0.7f, 1.f), H1 = half3(L1);
    const auto L2 = norm3(-0.3f, -0.5f, 1.f), H2 = half3(L2);
    const auto L3 = norm3(0.1f, 0.3f, 1.f);
    const auto L4 = norm3(0.f, 0.9f, 0.4f), H4 = half3(L4);
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const float cx = x + 0.5f, cy = y + 0.5f;
            const float sdf = sdfRoundRect(cx, cy, fw, fh, m_r);
            Texel &t = m_t[size_t(y) * m_w + x];
            t.mask = q8(coverage(sdf));
            if (sdf > 1.f) continue;
            const float inside = std::max(-sdf, 0.f);
            const float d = std::max(inside, 0.5f);
            const Vec2 n = sdfNormal(cx, cy, fw, fh, m_r);           // outward, screen coordinates
            const float slope = bevelSlope(d, zR);
            const float hC = bevelHeight(d, zR);
            const float gx = -n.x * slope, gy = -n.y * slope;         // grad h: points into the shape
            const float thickNorm = (hC * 2.f) / std::max(zR * 2.f, 1.f);
            // dual surface refraction (entry + exit + through), scaled by the refraction strength
            float rx = (gx * k * 2.f + gx * k * thickNorm * 0.5f) * p.refraction * 30.f * S;
            float ry = (gy * k * 2.f + gy * k * thickNorm * 0.5f) * p.refraction * 30.f * S;
            // slight pull towards the centre, fading out beyond 2 zR so the deep interior is untouched
            const float px = cx - halfX, py = cy - halfY;
            const float pull = smooth(0.f, zR, inside) * (1.f - smooth(zR, 2.f * zR, inside));
            rx += (-px / std::max(halfX, 1.f)) * p.refraction * 4.f * S * pull;
            ry += (-py / std::max(halfY, 1.f)) * p.refraction * 4.f * S * pull;
            float mag = std::sqrt(rx * rx + ry * ry);
            if (mag > 1e-4f) {
                const float cap = capPx * std::tanh(mag / capPx);   // soft cap
                rx *= cap / mag;
                ry *= cap / mag;
                mag = cap;
            }
            const float edge = 1.f - smooth(0.f, maxD * 0.35f, inside);
            // surface normal N = normalize(-grad h, 1)
            const float invN = 1.f / std::sqrt(1.f + slope * slope);
            const float Nx = -gx * invN, Ny = -gy * invN, Nz = invN;   // screen coords (y down)
            const float caS = p.chroma * 18.f * (edge * 0.7f + 0.3f) * 2.f * S;
            const float cxo = Nx * caS, cyo = Ny * caS;
            t.rx = q16(rx); t.ry = q16(ry);
            t.cx = q16(cxo); t.cy = q16(cyo);
            t.edge = q8(edge);
            m_max = std::max(m_max, mag);
            m_reach = std::max(m_reach, mag + std::sqrt(cxo * cxo + cyo * cyo));
            t.depth = q8(smooth(0.f, zR, inside));
            // ---- light layers ----
            const float fres = std::pow(1.f - std::fabs(Nz), 4.f) * p.fresnel;
            const float Ny_up = -Ny;                                    // lights are specified with y up
            auto dot3 = [&](const std::array<float, 3> &v) { return Nx * v[0] + Ny_up * v[1] + Nz * v[2]; };
            const float sp1 = std::pow(std::max(dot3(H1), 0.f), 90.f);
            const float sp2 = std::pow(std::max(dot3(H2), 0.f), 50.f) * 0.3f;
            const float spB = std::pow(std::max(dot3(L3), 0.f), 6.f) * 0.1f;
            const float sp4 = std::pow(std::max(dot3(H4), 0.f), 120.f) * 0.6f;
            const float totalSpec = (sp1 + sp2 + spB + sp4) * p.specular;
            const float bw = 1.5f * S;
            float stroke = smooth(-bw - 1.f, -bw, sdf) * (1.f - smooth(-1.f, 0.f, sdf));
            const float topBias = 0.5f + 0.5f * (-py / std::max(halfY, 1.f));
            stroke *= 0.4f + 0.6f * topBias;
            const float rim = edge * p.edgeHighlight * 0.22f;
            const float glow = (1.f - smooth(0.f, 5.f * S, inside)) * p.edgeHighlight * 0.15f;
            const float env = (Ny_up * 0.5f + 0.5f) * fres * 0.08f;
            t.add = q8(totalSpec + rim + glow + stroke * p.edgeHighlight * 0.55f + env);
            t.wmix = q8(fres);  // mixed towards white by 0.2 * fresnel when rendering
        }
    }
}

float ShapeTable::magnitude(int x, int y) const {
    const float a = dx(x, y), b = dy(x, y);
    return std::sqrt(a * a + b * b);
}

QImage ShapeTable::maskImage() const {
    QImage img(m_w, m_h, QImage::Format_Alpha8);
    for (int y = 0; y < m_h; ++y) {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < m_w; ++x) line[x] = at(x, y).mask;
    }
    return img;
}

namespace {
struct Key {
    int w, h;
    float r;
    Params p;
    bool operator==(const Key &o) const { return w == o.w && h == o.h && r == o.r && p.sameShape(o.p); }
};
QMutex g_mutex;
std::list<std::pair<Key, std::shared_ptr<const ShapeTable>>> g_cache;
size_t g_bytes = 0;
constexpr size_t kCacheBytes = 48u << 20;
}

std::shared_ptr<const ShapeTable> ShapeTable::cached(int w, int h, float radius, const Params &p) {
    const Key k{w, h, radius, p};
    {
        QMutexLocker lock(&g_mutex);
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
            if (it->first == k) {
                g_cache.splice(g_cache.begin(), g_cache, it);
                return g_cache.front().second;
            }
        }
    }
    auto t = std::make_shared<const ShapeTable>(w, h, radius, p);
    QMutexLocker lock(&g_mutex);
    g_cache.emplace_front(k, t);
    g_bytes += t->bytes();
    while (g_bytes > kCacheBytes && g_cache.size() > 1) {
        g_bytes -= g_cache.back().second->bytes();
        g_cache.pop_back();
    }
    return t;
}

int ShapeTable::cacheSize() {
    QMutexLocker lock(&g_mutex);
    return int(g_cache.size());
}

void ShapeTable::clearCache() {
    QMutexLocker lock(&g_mutex);
    g_cache.clear();
    g_bytes = 0;
}

// --- shadow ---------------------------------------------------------------------------------------------

QImage shadowAlpha(int w, int h, float radius, int margin, float spread, float offsetY) {
    static QMutex mutex;
    static std::list<std::pair<QString, QImage>> cache;
    const QString key = QString("%1x%2 r%3 m%4 s%5 o%6").arg(w).arg(h).arg(radius).arg(margin).arg(spread).arg(offsetY);
    {
        QMutexLocker l(&mutex);
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->first == key) { cache.splice(cache.begin(), cache, it); return cache.front().second; }
    }
    QImage img(w + 2 * margin, h + 2 * margin, QImage::Format_Alpha8);
    const float r = clampRadius(float(w), float(h), radius);
    const float sp = std::max(spread, 1.f);
    for (int y = 0; y < img.height(); ++y) {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            const float sdf = sdfRoundRect(x + 0.5f - margin, y + 0.5f - margin - offsetY, float(w), float(h), r);
            const float d = std::max(sdf - 1.f, 0.f);
            const float outer = std::exp(-d * d / (sp * sp)) * 0.65f;
            const float contact = std::exp(-d * 0.08f / std::max(sp * 0.04f, 0.01f)) * 0.35f;
            // outside the shape only; the shape itself is opaque glass
            const float inside = sdfRoundRect(x + 0.5f - margin, y + 0.5f - margin, float(w), float(h), r);
            float a = inside < -1.f ? 0.f : (outer + contact) * (1.f - coverage(inside));
            if (inside > sp * 3.f) a = 0.f;
            line[x] = q8(a);
        }
    }
    QMutexLocker l(&mutex);
    cache.emplace_front(key, img);
    while (cache.size() > 64) cache.pop_back();
    return img;
}

// --- blur / saturate ------------------------------------------------------------------------------------

static void boxPass(const quint32 *src, quint32 *dst, int len, int stride, int count, int r, int srcStep, int dstStep) {
    const int window = 2 * r + 1;
    for (int line = 0; line < count; ++line) {
        const quint32 *s = src + size_t(line) * srcStep;
        quint32 *d = dst + size_t(line) * dstStep;
        unsigned a = 0, rr = 0, g = 0, b = 0;
        auto px = [&](int i) { return s[size_t(std::min(std::max(i, 0), len - 1)) * stride]; };
        for (int i = -r; i <= r; ++i) {
            const quint32 c = px(i);
            a += c >> 24; rr += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
        }
        for (int i = 0; i < len; ++i) {
            d[size_t(i) * stride] = ((a / window) << 24) | ((rr / window) << 16) | ((g / window) << 8) | (b / window);
            const quint32 out = px(i - r), in = px(i + r + 1);
            a += (in >> 24) - (out >> 24);
            rr += ((in >> 16) & 255) - ((out >> 16) & 255);
            g += ((in >> 8) & 255) - ((out >> 8) & 255);
            b += (in & 255) - (out & 255);
        }
    }
}

QImage boxBlur(const QImage &srcIn, int radius, int iterations) {
    QImage img = srcIn.format() == QImage::Format_ARGB32_Premultiplied ? srcIn : srcIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (radius <= 0 || img.isNull()) return img;
    const int w = img.width(), h = img.height();
    std::vector<quint32> a(size_t(w) * h), b(size_t(w) * h);
    for (int y = 0; y < h; ++y) memcpy(a.data() + size_t(y) * w, img.constScanLine(y), size_t(w) * 4);
    for (int it = 0; it < std::max(1, iterations); ++it) {
        boxPass(a.data(), b.data(), w, 1, h, radius, w, w);
        boxPass(b.data(), a.data(), h, w, w, radius, 1, 1);
    }
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) memcpy(out.scanLine(y), a.data() + size_t(y) * w, size_t(w) * 4);
    out.setDevicePixelRatio(srcIn.devicePixelRatio());
    return out;
}

void saturate(QImage &img, float amount) {
    if (img.isNull() || std::fabs(amount - 1.f) < 0.001f) return;
    if (img.format() != QImage::Format_ARGB32_Premultiplied) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int s = int(amount * 256.f);
    for (int y = 0; y < img.height(); ++y) {
        quint32 *p = reinterpret_cast<quint32 *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const quint32 c = p[x];
            const int a = c >> 24;
            if (a == 0) continue;
            const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            const int luma = (r * 54 + g * 183 + b * 19) >> 8;
            auto f = [&](int v) { return std::min(a, std::max(0, luma + (((v - luma) * s) >> 8))); };
            p[x] = (quint32(a) << 24) | (quint32(f(r)) << 16) | (quint32(f(g)) << 8) | quint32(f(b));
        }
    }
}

// --- sampling -------------------------------------------------------------------------------------------

namespace {
struct Grid {
    const quint32 *base;
    int stride, w, h;
    quint32 at(int x, int y) const {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return base[size_t(y) * stride + x];
    }
    // bilinear fetch of one channel (shift 16 = red, 8 = green, 0 = blue)
    float chan(float fx, float fy, int shift) const {
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = fx - x0, ty = fy - y0;
        const float a = float((at(x0, y0) >> shift) & 255), b = float((at(x0 + 1, y0) >> shift) & 255);
        const float c = float((at(x0, y0 + 1) >> shift) & 255), d = float((at(x0 + 1, y0 + 1) >> shift) & 255);
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    }
};
}

QImage sampleThrough(const QImage &soft, const QImage *sharp, QPoint origin, const ShapeTable &t, const SampleOptions &o) {
    const int w = t.width(), h = t.height();
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    const Grid G{reinterpret_cast<const quint32 *>(soft.constBits()), int(soft.bytesPerLine() / 4), soft.width(), soft.height()};
    const bool haveSharp = sharp && sharp->size() == soft.size();
    const Grid S = haveSharp ? Grid{reinterpret_cast<const quint32 *>(sharp->constBits()), int(sharp->bytesPerLine() / 4), sharp->width(), sharp->height()} : G;
    for (int y = 0; y < h; ++y) {
        quint32 *dst = reinterpret_cast<quint32 *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const Texel &tx = t.at(x, y);
            const int bx = origin.x() + x, by = origin.y() + y;
            if (!o.displace || (tx.rx == 0 && tx.ry == 0 && tx.cx == 0 && tx.cy == 0)) {
                dst[x] = G.at(bx, by) | 0xff000000u;
                continue;
            }
            const float fx = bx + tx.rx / 16.f, fy = by + tx.ry / 16.f;
            const float cx = tx.cx / 16.f, cy = tx.cy / 16.f;
            const float edgeMix = 1.f - o.edgeSharp * (tx.edge / 255.f);   // weight of the blurred sample
            const bool mixSharp = haveSharp && edgeMix < 0.999f;
            float rgb[3];
            const int shifts[3] = {16, 8, 0};
            const float sgn[3] = {1.f, 0.f, -1.f};
            for (int c = 0; c < 3; ++c) {
                const float sx = fx + cx * sgn[c], sy = fy + cy * sgn[c];
                float v = G.chan(sx, sy, shifts[c]);
                if (mixSharp) v = S.chan(sx, sy, shifts[c]) * (1.f - edgeMix) + v * edgeMix;
                rgb[c] = v;
            }
            const float gain = 1.f + 0.06f * (tx.depth / 255.f);
            auto cl = [&](float v) { return quint32(std::min(255.f, v * gain + 0.5f)); };
            dst[x] = 0xff000000u | (cl(rgb[0]) << 16) | (cl(rgb[1]) << 8) | cl(rgb[2]);
        }
    }
    return out;
}

void applyLightAndMask(QImage &img, const ShapeTable &t, float lightScale, bool light) {
    for (int y = 0; y < img.height() && y < t.height(); ++y) {
        quint32 *p = reinterpret_cast<quint32 *>(img.scanLine(y));
        for (int x = 0; x < img.width() && x < t.width(); ++x) {
            const Texel &tx = t.at(x, y);
            quint32 c = p[x];
            if (tx.mask == 0) { p[x] = 0; continue; }
            if (light && (tx.add || tx.wmix)) {
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                const float add = tx.add / 255.f * lightScale * 255.f;
                const float wm = tx.wmix / 255.f * 0.2f * lightScale;
                auto f = [&](int v) { return std::min(255, int(v + add + (255 - v) * wm + 0.5f)); };
                r = f(r); g = f(g); b = f(b);
                c = 0xff000000u | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
            }
            if (tx.mask != 255) {
                const unsigned m = tx.mask;
                auto s = [&](quint32 v) { return (v * m + 127) / 255; };
                c = (s(c >> 24) << 24) | (s((c >> 16) & 255) << 16) | (s((c >> 8) & 255) << 8) | s(c & 255);
            }
            p[x] = c;
        }
    }
}

}  // namespace glass
