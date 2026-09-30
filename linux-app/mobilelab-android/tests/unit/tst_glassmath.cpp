#include <QtTest>
#include <cmath>
#include "glass/GlassMath.h"

using namespace glass;

class TstGlassMath : public QObject {
    Q_OBJECT
private slots:
    void sdfSignsAndEdge() {
        // 100 x 40 rounded rectangle, radius 10: centre inside, far outside positive, edge zero.
        QVERIFY(sdfRoundRect(50, 20, 100, 40, 10) < -19.9f);
        QVERIFY(sdfRoundRect(-30, 20, 100, 40, 10) > 29.9f);
        QCOMPARE(sdfRoundRect(0, 20, 100, 40, 10), 0.f);      // left edge, straight part
        QCOMPARE(sdfRoundRect(50, 0, 100, 40, 10), 0.f);      // top edge
        // Capsule: radius is clamped to half the height.
        QCOMPARE(sdfRoundRect(50, 20, 100, 40, 999), sdfRoundRect(50, 20, 100, 40, 20));
        // Corner arc: the diagonal point of a circle of radius 10 in a 20x20 square is at distance r*(1-1/sqrt2) from the corner.
        const float d = sdfRoundRect(10 - 10 / std::sqrt(2.f), 10 - 10 / std::sqrt(2.f), 20, 20, 10);
        QVERIFY(std::fabs(d) < 1e-4f);
    }

    void normalsPointOutwards() {
        Vec2 n = sdfNormal(0.5f, 20, 100, 40, 10);
        QCOMPARE(n.x, -1.f);
        QCOMPARE(n.y, 0.f);
        n = sdfNormal(50, 39.5f, 100, 40, 10);
        QCOMPARE(n.y, 1.f);
        n = sdfNormal(1, 1, 100, 40, 10);  // top left corner: pointing up-left
        QVERIFY(n.x < 0 && n.y < 0);
        QVERIFY(std::fabs(std::hypot(n.x, n.y) - 1.f) < 1e-5f);
    }

    void displacementProfile() {
        Params p;
        QVERIFY(displacementAt(0, p) >= displacementAt(1, p));
        float prev = displacementAt(0, p);
        for (int d = 1; d < 80; ++d) {
            const float m = displacementAt(float(d), p);
            QVERIFY2(m <= prev + 1e-6f, "profile must decay monotonically");
            prev = m;
        }
        QVERIFY(displacementAt(0, p) <= p.edge + p.rim + 1e-4f);
        QVERIFY(displacementAt(70, p) < 0.05f);
    }

    void tableSymmetry() {
        Params p;
        RefractionTable t(120, 34, 17, p);
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x) {
                const int mx = t.width() - 1 - x, my = t.height() - 1 - y;
                QVERIFY2(std::fabs(t.dx(x, y) + t.dx(mx, y)) < 1e-4f, "dx antisymmetric horizontally");
                QVERIFY2(std::fabs(t.dy(x, y) - t.dy(mx, y)) < 1e-4f, "dy symmetric horizontally");
                QVERIFY2(std::fabs(t.dx(x, y) - t.dx(x, my)) < 1e-4f, "dx symmetric vertically");
                QVERIFY2(std::fabs(t.dy(x, y) + t.dy(x, my)) < 1e-4f, "dy antisymmetric vertically");
                QCOMPARE(t.mask(x, y), t.mask(mx, y));
                QCOMPARE(t.mask(x, y), t.mask(x, my));
            }
    }

    void zeroDeepInside() {
        Params p;
        RefractionTable t(241, 161, 30, p);
        int deep = 0;
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x) {
                const float depth = -sdfRoundRect(x + 0.5f, y + 0.5f, 241, 161, 30);
                if (depth >= 4 * p.rimWidth) {
                    ++deep;
                    QVERIFY2(t.magnitude(x, y) < 0.1f, "no displacement deep inside");
                }
            }
        QVERIFY(deep > 1000);
        QCOMPARE(t.magnitude(120, 80), 0.f);  // exact centre of an odd sized shape
        QCOMPARE(t.mask(120, 80), quint8(255));
    }

    void maximumAtEdge() {
        Params p;
        RefractionTable t(200, 80, 20, p);
        float best = 0;
        int bx = 0, by = 0;
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x)
                if (t.magnitude(x, y) > best) { best = t.magnitude(x, y); bx = x; by = y; }
        const float depth = -sdfRoundRect(bx + 0.5f, by + 0.5f, 200, 80, 20);
        QVERIFY2(depth < 1.0f, "the maximum displacement sits on the edge");
        QCOMPARE(best, t.maxMagnitude());
        QVERIFY(best >= p.edge);
        // Along the vertical centre line the magnitude falls from the top edge towards the centre.
        float prev = 1e9f;
        for (int y = 0; y < 40; ++y) {
            const float m = t.magnitude(100, y);
            QVERIFY(m <= prev + 1e-5f);
            prev = m;
        }
        // Points at the edge move outwards (up at the top edge).
        QVERIFY(t.dy(100, 0) < 0);
        QVERIFY(t.dy(100, 79) > 0);
        QVERIFY(t.dx(0, 40) < 0);
        QVERIFY(t.dx(199, 40) > 0);
    }

    void maskAntiAliasing() {
        Params p;
        RefractionTable t(64, 32, 16, p);
        QCOMPARE(t.mask(32, 16), quint8(255));
        QCOMPARE(t.mask(0, 0), quint8(0));                       // corner outside the arc
        int partial = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x)
                if (t.mask(x, y) > 0 && t.mask(x, y) < 255) ++partial;
        QVERIFY2(partial > 20, "edge pixels carry fractional coverage");
        // Coverage is 0.5 exactly on the edge and monotonic across it.
        QVERIFY(std::fabs(coverage(0.f) - 0.5f) < 1e-6f);
        QCOMPARE(coverage(-1.f), 1.f);
        QCOMPARE(coverage(1.f), 0.f);
        float prev = 1.f;
        for (float s = -1.5f; s <= 1.5f; s += 0.1f) {
            QVERIFY(coverage(s) <= prev + 1e-6f);
            prev = coverage(s);
        }
        // Straight left edge: first pixel column is inside by half a pixel -> partial but mostly covered.
        QVERIFY(t.mask(0, 16) > 190 && t.mask(0, 16) < 255);
        QImage m = t.maskImage();
        QCOMPARE(m.format(), QImage::Format_Alpha8);
        QCOMPARE(qAlpha(m.pixel(32, 16)), 255);
    }

    void tableCacheIsShared() {
        RefractionTable::clearCache();
        Params p;
        auto a = RefractionTable::cached(80, 30, 15, p);
        auto b = RefractionTable::cached(80, 30, 15, p);
        auto c = RefractionTable::cached(81, 30, 15, p);
        QCOMPARE(a.get(), b.get());
        QVERIFY(a.get() != c.get());
        QCOMPARE(RefractionTable::cacheSize(), 2);
        Params q = p;
        q.edge = 3;
        QVERIFY(RefractionTable::cached(80, 30, 15, q).get() != a.get());
    }

    void blurKeepsFlatAndSpreadsEdges() {
        QImage flat(40, 40, QImage::Format_ARGB32_Premultiplied);
        flat.fill(QColor(120, 60, 200));
        QImage b = boxBlur(flat, 6, 2);
        QCOMPARE(b.pixelColor(20, 20), QColor(120, 60, 200));
        QCOMPARE(b.pixelColor(0, 0), QColor(120, 60, 200));   // edge clamping keeps flat images flat
        QImage stripes(60, 20, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < 60; ++x)
            for (int y = 0; y < 20; ++y) stripes.setPixelColor(x, y, x < 30 ? Qt::black : Qt::white);
        QImage s = boxBlur(stripes, 5, 2);
        QVERIFY(s.pixelColor(10, 10).red() < 5);
        QVERIFY(s.pixelColor(50, 10).red() > 250);
        const int mid = s.pixelColor(30, 10).red();
        QVERIFY2(mid > 100 && mid < 160, "edge is smoothed to mid grey");
        QVERIFY(s.pixelColor(26, 10).red() > s.pixelColor(20, 10).red());
    }

    void saturateBehaves() {
        QImage img(2, 1, QImage::Format_ARGB32_Premultiplied);
        img.setPixelColor(0, 0, QColor(200, 100, 100));
        img.setPixelColor(1, 0, QColor(128, 128, 128));
        saturate(img, 1.6f);
        const QColor c = img.pixelColor(0, 0);
        QVERIFY(c.red() - c.green() > 100);            // more colourful
        QCOMPARE(img.pixelColor(1, 0), QColor(128, 128, 128));  // grey stays grey
        saturate(img, 0.f);
        QVERIFY(std::abs(img.pixelColor(0, 0).red() - img.pixelColor(0, 0).green()) <= 2);
    }

    void refractionBendsStripesAtTheEdgeOnly() {
        // Vertical stripes 6 px wide behind a 120x40 capsule: the middle of the capsule samples the
        // untouched backdrop, the ends sample displaced positions.
        const int M = 20, W = 120, H = 40;
        QImage back(W + 2 * M, H + 2 * M, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < back.width(); ++x)
            for (int y = 0; y < back.height(); ++y) back.setPixelColor(x, y, ((x / 6) % 2) ? Qt::white : Qt::black);
        Params p;
        RefractionTable t(W, H, 20, p);
        QImage plain = refract(back, QPoint(M, M), t, false);
        QImage bent = refract(back, QPoint(M, M), t, true);
        int diffCentre = 0, diffEdge = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const bool differs = qAbs(qRed(plain.pixel(x, y)) - qRed(bent.pixel(x, y))) > 40;
                const float depth = -sdfRoundRect(x + 0.5f, y + 0.5f, W, H, 20);
                if (differs && depth > 40 * 0.5f) ++diffCentre;
                if (differs && depth < 4.f) ++diffEdge;
            }
        QCOMPARE(diffCentre, 0);
        QVERIFY2(diffEdge > 40, "stripes must be visibly displaced at the edge");
    }
};

QTEST_APPLESS_MAIN(TstGlassMath)
#include "tst_glassmath.moc"
