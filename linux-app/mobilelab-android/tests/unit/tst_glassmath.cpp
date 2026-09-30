#include <QtTest>
#include <cmath>
#include "glass/GlassMath.h"

using namespace glass;

static Params testParams() {
    Params p;
    p.zRadius = 20;
    p.refraction = 0.69f;
    p.chroma = 0.05f;
    return p;
}

class TstGlassMath : public QObject {
    Q_OBJECT
private slots:
    void sdfSignsAndEdge() {
        QVERIFY(sdfRoundRect(50, 20, 100, 40, 10) < -19.9f);
        QVERIFY(sdfRoundRect(-30, 20, 100, 40, 10) > 29.9f);
        QCOMPARE(sdfRoundRect(0, 20, 100, 40, 10), 0.f);
        QCOMPARE(sdfRoundRect(50, 0, 100, 40, 10), 0.f);
        QCOMPARE(sdfRoundRect(50, 20, 100, 40, 999), sdfRoundRect(50, 20, 100, 40, 20));  // capsule clamp
        const float d = sdfRoundRect(10 - 10 / std::sqrt(2.f), 10 - 10 / std::sqrt(2.f), 20, 20, 10);
        QVERIFY(std::fabs(d) < 1e-4f);
    }

    void normalsPointOutwards() {
        Vec2 n = sdfNormal(0.5f, 20, 100, 40, 10);
        QCOMPARE(n.x, -1.f);
        QCOMPARE(n.y, 0.f);
        n = sdfNormal(50, 39.5f, 100, 40, 10);
        QCOMPARE(n.y, 1.f);
        n = sdfNormal(1, 1, 100, 40, 10);
        QVERIFY(n.x < 0 && n.y < 0);
        QVERIFY(std::fabs(std::hypot(n.x, n.y) - 1.f) < 1e-5f);
    }

    void bevelIsAHalfCircle() {
        const float zR = 20;
        QCOMPARE(bevelHeight(0, zR), 0.f);
        QCOMPARE(bevelHeight(-3, zR), 0.f);
        QCOMPARE(bevelHeight(zR, zR), zR);
        QCOMPARE(bevelHeight(35, zR), zR);
        // On the circle of radius zR centred at depth zR: (d - zR)^2 + h^2 = zR^2
        for (float d = 1; d < zR; d += 1.5f) {
            const float h = bevelHeight(d, zR);
            QVERIFY(std::fabs((d - zR) * (d - zR) + h * h - zR * zR) < 1e-3f);
        }
        float prevH = 0, prevS = 1e9f;
        for (float d = 0.5f; d < zR; d += 0.5f) {
            QVERIFY(bevelHeight(d, zR) > prevH);           // rising
            QVERIFY(bevelSlope(d, zR) < prevS);            // steepest at the edge, flat in the middle
            prevH = bevelHeight(d, zR);
            prevS = bevelSlope(d, zR);
        }
        QCOMPARE(bevelSlope(zR, zR), 0.f);
    }

    void tableSymmetry() {
        const Params p = testParams();
        ShapeTable t(120, 34, 17, p);
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x) {
                const int mx = t.width() - 1 - x, my = t.height() - 1 - y;
                QVERIFY2(std::fabs(t.dx(x, y) + t.dx(mx, y)) < 0.13f, "refraction dx antisymmetric horizontally");
                QVERIFY2(std::fabs(t.dy(x, y) - t.dy(mx, y)) < 0.13f, "refraction dy symmetric horizontally");
                QVERIFY2(std::fabs(t.dx(x, y) - t.dx(x, my)) < 0.13f, "refraction dx symmetric vertically");
                QVERIFY2(std::fabs(t.dy(x, y) + t.dy(x, my)) < 0.13f, "refraction dy antisymmetric vertically");
                QVERIFY2(std::fabs(t.cx(x, y) + t.cx(mx, y)) < 0.13f, "chroma vector mirrors too");
                QCOMPARE(t.mask(x, y), t.mask(mx, y));
                QCOMPARE(t.mask(x, y), t.mask(x, my));
                QCOMPARE(t.at(x, y).edge, t.at(mx, my).edge);
                QCOMPARE(t.at(x, y).depth, t.at(mx, y).depth);
            }
    }

    void zeroDeepInside() {
        const Params p = testParams();
        ShapeTable t(261, 181, 30, p);
        int deep = 0;
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x) {
                const float depth = -sdfRoundRect(x + 0.5f, y + 0.5f, 261, 181, 30);
                if (depth >= 2 * t.zRadius() + 1) {
                    ++deep;
                    QCOMPARE(t.magnitude(x, y), 0.f);       // refraction and the centre pull have both faded out
                    QCOMPARE(t.cx(x, y), 0.f);
                    QCOMPARE(t.cy(x, y), 0.f);
                    QCOMPARE(t.at(x, y).edge, quint8(0));
                }
            }
        QVERIFY(deep > 2000);
        QCOMPARE(t.magnitude(130, 90), 0.f);
        QCOMPARE(t.mask(130, 90), quint8(255));
    }

    void maximumAtEdge() {
        const Params p = testParams();
        ShapeTable t(200, 90, 20, p);
        float best = 0;
        int bx = 0, by = 0;
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x)
                if (t.magnitude(x, y) > best) { best = t.magnitude(x, y); bx = x; by = y; }
        const float depth = -sdfRoundRect(bx + 0.5f, by + 0.5f, 200, 90, 20);
        QVERIFY2(depth < 2.0f, "the maximum displacement sits on the edge");
        QCOMPARE(best, t.maxMagnitude());
        QVERIFY(best <= t.zRadius() + 0.1f);                    // soft cap
        QVERIFY(best > 5.f);
        // Along the vertical centre line the magnitude falls from the top edge into the interior.
        float prev = 1e9f;
        for (int y = 0; y < int(t.zRadius()); ++y) {
            const float m = t.magnitude(100, y);
            QVERIFY2(m <= prev + 0.07f, qPrintable(QString("row %1: %2 > %3").arg(y).arg(m).arg(prev)));
            prev = m;
        }
        QVERIFY(t.magnitude(100, 0) > 3 * t.magnitude(100, 15));
        // Refraction points into the shape (the reference model samples towards the centre).
        QVERIFY(t.dy(100, 0) > 0);
        QVERIFY(t.dy(100, 89) < 0);
        QVERIFY(t.dx(0, 45) > 0);
        QVERIFY(t.dx(199, 45) < 0);
        // Chromatic aberration is along the outward normal: red samples further out than blue.
        QVERIFY(t.cy(100, 0) < 0);
        QVERIFY(t.cy(100, 89) > 0);
    }

    void lightAndFresnelLiveOnTheBevel() {
        const Params p = testParams();
        ShapeTable t(160, 60, 30, p);
        // The precomputed light is strongest on the bevel, zero in the flat interior.
        float edgeLight = 0, midLight = 0;
        for (int x = 40; x < 120; ++x) { edgeLight += t.light(x, 1); midLight += t.light(x, 30); }
        QVERIFY(edgeLight > 5 * midLight);
        // light comes from the top: the top edge is brighter than the bottom edge
        float top = 0, bottom = 0;
        for (int x = 40; x < 120; ++x) { top += t.light(x, 1); bottom += t.light(x, 58); }
        QVERIFY2(top > bottom * 1.1f, "top biased inner stroke and lobes");
        QVERIFY(t.at(80, 1).wmix > t.at(80, 30).wmix);          // Fresnel: grazing angles at the edge
        Params off = p;
        off.specular = 0; off.edgeHighlight = 0;
        ShapeTable dark(160, 60, 30, off);
        float sum = 0;
        for (int y = 0; y < 60; ++y) for (int x = 0; x < 160; ++x) sum += dark.light(x, y);
        QVERIFY(sum < 60.f * 160 * 0.01f);                      // only the faint environment term remains
    }

    void maskAntiAliasing() {
        ShapeTable t(64, 32, 16, testParams());
        QCOMPARE(t.mask(32, 16), quint8(255));
        QCOMPARE(t.mask(0, 0), quint8(0));
        int partial = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x)
                if (t.mask(x, y) > 0 && t.mask(x, y) < 255) ++partial;
        QVERIFY2(partial > 20, "edge pixels carry fractional coverage");
        QVERIFY(std::fabs(coverage(0.f) - 0.5f) < 1e-6f);
        QCOMPARE(coverage(-1.f), 1.f);
        QCOMPARE(coverage(1.f), 0.f);
        float prev = 1.f;
        for (float s = -1.5f; s <= 1.5f; s += 0.1f) {
            QVERIFY(coverage(s) <= prev + 1e-6f);
            prev = coverage(s);
        }
        QVERIFY(t.mask(0, 16) > 190 && t.mask(0, 16) < 255);
        QImage m = t.maskImage();
        QCOMPARE(m.format(), QImage::Format_Alpha8);
        QCOMPARE(qAlpha(m.pixel(32, 16)), 255);
    }

    void shadowIsOutsideAndOffsetDown() {
        const int W = 100, H = 40, M = 12;
        const QImage a = shadowAlpha(W, H, 20, M, 4.f, 1.f);
        QCOMPARE(a.width(), W + 2 * M);
        QCOMPARE(a.height(), H + 2 * M);
        auto at = [&](int x, int y) { return qAlpha(a.pixel(x, y)); };
        QCOMPARE(at(M + W / 2, M + H / 2), 0);                        // under the glass: nothing
        QVERIFY(at(M + W / 2, M + H + 1) > 60);                        // just below the shape
        QVERIFY(at(M + W / 2, M + H + 1) > at(M + W / 2, M - 2));      // offset down: bottom stronger than top
        QVERIFY(at(M + W / 2, M + H + 1) > at(M + W / 2, M + H + 9));  // falls off outwards
        QCOMPARE(at(0, 0), 0);                                        // corner far away
        QVERIFY(&a != nullptr);
        // same key returns the same pixels (cached)
        QCOMPARE(shadowAlpha(W, H, 20, M, 4.f, 1.f), a);
    }

    void tableCacheIsShared() {
        ShapeTable::clearCache();
        const Params p = testParams();
        auto a = ShapeTable::cached(80, 30, 15, p);
        auto b = ShapeTable::cached(80, 30, 15, p);
        auto c = ShapeTable::cached(81, 30, 15, p);
        QCOMPARE(a.get(), b.get());
        QVERIFY(a.get() != c.get());
        QCOMPARE(ShapeTable::cacheSize(), 2);
        Params q = p;
        q.refraction = 0.1f;
        QVERIFY(ShapeTable::cached(80, 30, 15, q).get() != a.get());
        // frosting parameters do not enter the table, so they share it
        Params r = p;
        r.blur = 99;
        QCOMPARE(ShapeTable::cached(80, 30, 15, r).get(), a.get());
        QVERIFY(a->bytes() >= size_t(80 * 30 * sizeof(Texel)));
    }

    void blurKeepsFlatAndSpreadsEdges() {
        QImage flat(40, 40, QImage::Format_ARGB32_Premultiplied);
        flat.fill(QColor(120, 60, 200));
        QImage b = boxBlur(flat, 6, 2);
        QCOMPARE(b.pixelColor(20, 20), QColor(120, 60, 200));
        QCOMPARE(b.pixelColor(0, 0), QColor(120, 60, 200));
        QImage stripes(60, 20, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < 60; ++x)
            for (int y = 0; y < 20; ++y) stripes.setPixelColor(x, y, x < 30 ? Qt::black : Qt::white);
        QImage s = boxBlur(stripes, 5, 2);
        QVERIFY(s.pixelColor(10, 10).red() < 5);
        QVERIFY(s.pixelColor(50, 10).red() > 250);
        const int mid = s.pixelColor(30, 10).red();
        QVERIFY2(mid > 100 && mid < 160, "edge is smoothed to mid grey");
    }

    void saturateBehaves() {
        QImage img(2, 1, QImage::Format_ARGB32_Premultiplied);
        img.setPixelColor(0, 0, QColor(200, 100, 100));
        img.setPixelColor(1, 0, QColor(128, 128, 128));
        saturate(img, 1.6f);
        const QColor c = img.pixelColor(0, 0);
        QVERIFY(c.red() - c.green() > 100);
        QCOMPARE(img.pixelColor(1, 0), QColor(128, 128, 128));
        saturate(img, 0.f);
        QVERIFY(std::abs(img.pixelColor(0, 0).red() - img.pixelColor(0, 0).green()) <= 2);
    }

    void samplingBendsStripesAtTheEdgeOnly() {
        const int M = 40, W = 160, H = 60;
        QImage back(W + 2 * M, H + 2 * M, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < back.width(); ++x)
            for (int y = 0; y < back.height(); ++y) back.setPixelColor(x, y, ((x / 6) % 2) ? Qt::white : Qt::black);
        Params p = testParams();
        ShapeTable t(W, H, 30, p);
        SampleOptions plainOpt; plainOpt.displace = false;
        SampleOptions bentOpt; bentOpt.displace = true;
        const QImage plain = sampleThrough(back, &back, QPoint(M, M), t, plainOpt);
        const QImage bent = sampleThrough(back, &back, QPoint(M, M), t, bentOpt);
        int diffDeep = 0, diffEdge = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const bool differs = qAbs(qRed(plain.pixel(x, y)) - qRed(bent.pixel(x, y))) > 40;
                const float depth = -sdfRoundRect(x + 0.5f, y + 0.5f, W, H, 30);
                if (differs && depth > 2 * t.zRadius()) ++diffDeep;
                if (differs && depth < 8.f) ++diffEdge;
            }
        QCOMPARE(diffDeep, 0);
        QVERIFY2(diffEdge > 100, "stripes must be visibly displaced at the edge");
    }

    void lightPassAddsLightAndMask() {
        Params p = testParams();
        ShapeTable t(100, 40, 20, p);
        QImage base(100, 40, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(60, 60, 60));
        QImage lit = base;
        applyLightAndMask(lit, t, 1.f, true);
        QImage dim = base;
        applyLightAndMask(dim, t, 1.f, false);
        QCOMPARE(qAlpha(lit.pixel(50, 20)), 255);
        QCOMPARE(qAlpha(lit.pixel(0, 0)), 0);                          // outside the capsule
        QVERIFY(qAlpha(lit.pixel(0, 20)) < 255 && qAlpha(lit.pixel(0, 20)) > 0);   // anti-aliased edge
        const int edgeDelta = qRed(lit.pixel(50, 1)) - qRed(dim.pixel(50, 1));
        const int midDelta = qRed(lit.pixel(50, 20)) - qRed(dim.pixel(50, 20));
        QVERIFY(edgeDelta > 0);                                        // light brightens the bevel
        QVERIFY2(midDelta >= 0 && midDelta * 2 < edgeDelta, "the middle only gets the faint broad lobe");
    }
};

QTEST_APPLESS_MAIN(TstGlassMath)
#include "tst_glassmath.moc"
