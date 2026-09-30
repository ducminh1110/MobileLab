#include "GlassMath.h"
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <tuple>

namespace glass {

bool Params::operator==(const Params &o) const {
    return blur == o.blur && saturate == o.saturate && edge == o.edge && edgeWidth == o.edgeWidth && rim == o.rim &&
           rimWidth == o.rimWidth && base == o.base && baseWidth == o.baseWidth && cornerBoost == o.cornerBoost &&
           refract == o.refract;
}

static float clampRadius(float w, float h, float r) { return std::max(0.f, std::min(r, std::min(w, h) * 0.5f)); }

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
    Vec2 n;
    if (qx > 0 && qy > 0) {  // corner arc region
        const float l = std::sqrt(qx * qx + qy * qy);
        n = {qx / l * sx, qy / l * sy};
    } else if (qx > qy) {  // closest to a vertical side
        n = {sx, 0.f};
    } else if (qy > qx) {  // closest to a horizontal side
        n = {0.f, sy};
    } else {  // exactly on the medial axis: no preferred direction
        n = {0.f, 0.f};
    }
    return n;
}

float coverage(float sdf) {
    const float t = std::min(1.f, std::max(0.f, (sdf + 1.f) * 0.5f));
    return 1.f - t * t * (3.f - 2.f * t);
}

float displacementAt(float depth, const Params &p) {
    if (depth < 0.f) depth = 0.f;
    float m = 0.f;
    if (p.edge > 0.f && p.edgeWidth > 0.f) m += p.edge * std::exp(-depth / p.edgeWidth);
    if (p.rim > 0.f && p.rimWidth > 0.f) m += p.rim * std::exp(-depth / p.rimWidth);
    // The exponential tails are cut to exactly zero between 3 and 5 widths so the centre of a large
    // shape is untouched (and the table is sparse).
    const float w = std::max(p.edgeWidth, p.rimWidth);
    if (w > 0.f) {
        const float t = std::min(1.f, std::max(0.f, (depth - 3.f * w) / (2.f * w)));
        m *= 1.f - t * t * (3.f - 2.f * t);
    }
    if (p.base > 0.f && p.baseWidth > 0.f) m += p.base * (1.f - std::exp(-depth / p.baseWidth));
    return m;
}

RefractionTable::RefractionTable(int w, int h, float radius, const Params &p)
    : m_w(std::max(1, w)), m_h(std::max(1, h)), m_r(clampRadius(float(w), float(h), radius)),
      m_dx(size_t(m_w) * m_h), m_dy(size_t(m_w) * m_h), m_mask(size_t(m_w) * m_h) {
    const float fw = float(m_w), fh = float(m_h);
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const float cx = x + 0.5f, cy = y + 0.5f;
            const float sdf = sdfRoundRect(cx, cy, fw, fh, m_r);
            const size_t i = size_t(y) * m_w + x;
            m_mask[i] = quint8(std::lround(coverage(sdf) * 255.f));
            if (!p.refract || sdf > 0.f) {
                m_dx[i] = m_dy[i] = 0.f;
                continue;
            }
            const float depth = -sdf;
            float mag = displacementAt(depth, p);
            if (p.cornerBoost > 0.f && m_r > 0.f) {
                const float px = cx - fw * 0.5f, py = cy - fh * 0.5f;
                const float qx = std::fabs(px) - (fw * 0.5f - m_r), qy = std::fabs(py) - (fh * 0.5f - m_r);
                const float t = std::min(1.f, std::max(0.f, (std::min(qx, qy) + 0.5f * m_r) / (0.75f * m_r)));
                mag *= 1.f + p.cornerBoost * t * t * (3.f - 2.f * t);
            }
            const Vec2 n = sdfNormal(cx, cy, fw, fh, m_r);
            m_dx[i] = n.x * mag;
            m_dy[i] = n.y * mag;
            m_max = std::max(m_max, mag);
        }
    }
}

float RefractionTable::magnitude(int x, int y) const {
    const float a = dx(x, y), b = dy(x, y);
    return std::sqrt(a * a + b * b);
}

QImage RefractionTable::maskImage() const {
    QImage img(m_w, m_h, QImage::Format_Alpha8);
    for (int y = 0; y < m_h; ++y) memcpy(img.scanLine(y), m_mask.data() + size_t(y) * m_w, size_t(m_w));
    return img;
}

namespace {
struct Key {
    int w, h;
    float r;
    Params p;
    bool operator==(const Key &o) const { return w == o.w && h == o.h && r == o.r && p == o.p; }
};
QMutex g_mutex;
std::list<std::pair<Key, std::shared_ptr<const RefractionTable>>> g_cache;
constexpr size_t kCacheMax = 48;
}

std::shared_ptr<const RefractionTable> RefractionTable::cached(int w, int h, float radius, const Params &p) {
    const Key k{w, h, radius, p};
    QMutexLocker lock(&g_mutex);
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
        if (it->first == k) {
            g_cache.splice(g_cache.begin(), g_cache, it);  // most recently used first
            return g_cache.front().second;
        }
    }
    auto t = std::make_shared<const RefractionTable>(w, h, radius, p);
    g_cache.emplace_front(k, t);
    while (g_cache.size() > kCacheMax) g_cache.pop_back();
    return t;
}

int RefractionTable::cacheSize() {
    QMutexLocker lock(&g_mutex);
    return int(g_cache.size());
}

void RefractionTable::clearCache() {
    QMutexLocker lock(&g_mutex);
    g_cache.clear();
}

// --- blur -------------------------------------------------------------------------------------------

static void boxPass(const quint32 *src, quint32 *dst, int len, int stride, int count, int r, int srcStep, int dstStep) {
    // Running-sum box blur along one axis, clamped at both ends. Processes `count` lines.
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
        boxPass(a.data(), b.data(), w, 1, h, radius, w, w);   // horizontal
        boxPass(b.data(), a.data(), h, w, w, radius, 1, 1);   // vertical
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

QImage refract(const QImage &bd, QPoint origin, const RefractionTable &t, bool displace, const QImage *sharp) {
    const int w = t.width(), h = t.height();
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    const int bw = bd.width(), bh = bd.height();
    const quint32 *base = reinterpret_cast<const quint32 *>(bd.constBits());
    const int stride = int(bd.bytesPerLine() / 4);
    const bool useSharp = sharp && displace && sharp->size() == bd.size();
    const quint32 *sbase = useSharp ? reinterpret_cast<const quint32 *>(sharp->constBits()) : nullptr;
    const int sstride = useSharp ? int(sharp->bytesPerLine() / 4) : 0;
    const float maxMag = std::max(0.001f, t.maxMagnitude());
    auto at = [&](const quint32 *b, int st, int x, int y) {
        x = std::min(std::max(x, 0), bw - 1);
        y = std::min(std::max(y, 0), bh - 1);
        return b[size_t(y) * st + x];
    };
    auto bilinear = [&](const quint32 *b, int st, float fx, float fy) {
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = fx - x0, ty = fy - y0;
        const quint32 c00 = at(b, st, x0, y0), c10 = at(b, st, x0 + 1, y0), c01 = at(b, st, x0, y0 + 1), c11 = at(b, st, x0 + 1, y0 + 1);
        float r[3];
        int i = 0;
        for (int shift : {16, 8, 0}) {
            const float top = ((c00 >> shift) & 255) * (1 - tx) + ((c10 >> shift) & 255) * tx;
            const float bot = ((c01 >> shift) & 255) * (1 - tx) + ((c11 >> shift) & 255) * tx;
            r[i++] = top * (1 - ty) + bot * ty;
        }
        return std::array<float, 3>{r[0], r[1], r[2]};
    };
    for (int y = 0; y < h; ++y) {
        quint32 *o = reinterpret_cast<quint32 *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int bx = origin.x() + x, by = origin.y() + y;
            const float ddx = displace ? t.dx(x, y) : 0.f, ddy = displace ? t.dy(x, y) : 0.f;
            if (ddx == 0.f && ddy == 0.f) {
                o[x] = at(base, stride, bx, by) | 0xff000000u;
                continue;
            }
            auto c = bilinear(base, stride, bx + ddx, by + ddy);
            if (useSharp) {
                const float m = std::sqrt(ddx * ddx + ddy * ddy) / maxMag;
                float k = std::min(1.f, std::max(0.f, (m - 0.30f) / 0.40f));
                k = k * k * (3.f - 2.f * k);
                if (k > 0.f) {
                    const auto s = bilinear(sbase, sstride, bx + ddx, by + ddy);
                    for (int i = 0; i < 3; ++i) c[i] += (s[i] - c[i]) * k;
                }
            }
            o[x] = 0xff000000u | (quint32(c[0] + 0.5f) << 16) | (quint32(c[1] + 0.5f) << 8) | quint32(c[2] + 0.5f);
        }
    }
    return out;
}

void applyMask(QImage &img, const RefractionTable &t) {
    for (int y = 0; y < img.height() && y < t.height(); ++y) {
        quint32 *p = reinterpret_cast<quint32 *>(img.scanLine(y));
        for (int x = 0; x < img.width() && x < t.width(); ++x) {
            const unsigned m = t.mask(x, y);
            if (m == 255) continue;
            const quint32 c = p[x];
            if (m == 0) { p[x] = 0; continue; }
            auto s = [&](quint32 v) { return (v * m + 127) / 255; };
            p[x] = (s(c >> 24) << 24) | (s((c >> 16) & 255) << 16) | (s((c >> 8) & 255) << 8) | s(c & 255);
        }
    }
}

}  // namespace glass
