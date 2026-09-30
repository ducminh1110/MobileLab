// mobilelab-glass-proof OUT.png: draws glass capsules over a striped backdrop at every level and
// verifies that refraction bends the stripes at the edges, that blur keeps them straight, and that the
// no-backdrop fallback is a flat tint. Prints the measured material render times.
#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QSettings>
#include <cstdio>
#include "glass/Glass.h"

class Proof : public QWidget {
public:
    struct Cell { QString title; std::optional<Glass::Level> level; bool noBackdrop; };
    Proof() {
        resize(1000, 560);
        const QVector<Cell> cells = {{"Off (opaque)", Glass::Level::Off, false},
                                     {"Blur", Glass::Level::Blur, false},
                                     {"Full (refraction)", Glass::Level::Full, false},
                                     {"No backdrop (fallback)", Glass::Level::Full, true}};
        int col = 0;
        for (const auto &c : cells) {
            const int x = 20 + col * 245;
            auto add = [&](GlassPanel::Shape shape, int px, int py, int w, int h, const QString &name, Glass::Kind kind = Glass::Kind::Control) {
                auto *g = new GlassPanel(this, kind);
                g->setObjectName(name + "-" + QString::number(col));
                g->setShape(shape, 18);
                g->setShadowMargin(4);
                g->forceLevel(c.level);
                g->setBackdropDisabled(c.noBackdrop);
                g->setGeometry(x + px - 4, py - 4, w + 8, h + 8);
                m_panels << g;
                return g;
            };
            add(GlassPanel::Shape::Capsule, 0, 80, 225, 40, "capsule");
            add(GlassPanel::Shape::Circle, 10, 170, 60, 60, "circle");
            add(GlassPanel::Shape::Capsule, 90, 180, 130, 34, "pill");
            add(GlassPanel::Shape::RoundRect, 0, 290, 225, 110, "roundrect");
            add(GlassPanel::Shape::Circle, 0, 440, 48, 48, "accent", Glass::Kind::Accent);
            add(GlassPanel::Shape::Capsule, 62, 446, 163, 36, "field", Glass::Kind::Field);
            m_titles << QPair<int, QString>(x, c.title);
            ++col;
        }
    }
    QVector<GlassPanel *> m_panels;
    QVector<QPair<int, QString>> m_titles;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // High contrast test pattern: vertical and diagonal stripes, coloured bars, a grid of dots.
        for (int x = 0; x < width(); x += 12) p.fillRect(x, 0, 6, height(), QColor(20, 24, 40));
        p.fillRect(rect(), QColor(255, 255, 255, 0));
        p.fillRect(QRect(0, 0, width(), 60), QColor("#f5f5f7"));
        p.setPen(QPen(QColor(255, 90, 60), 5));
        for (int x = -height(); x < width(); x += 46) p.drawLine(x, height(), x + height(), 0);
        p.setPen(QPen(QColor(60, 200, 120), 3));
        for (int y = 100; y < height(); y += 60) p.drawLine(0, y, width(), y);
        p.fillRect(QRect(0, 0, width(), 60), QColor("#f5f5f7"));
        p.setPen(Qt::black);
        QFont f = Theme::instance().ui(14, QFont::DemiBold);
        p.setFont(f);
        for (const auto &t : m_titles) p.drawText(t.first, 38, t.second);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::ensureFonts();
    Theme::instance().setMode(qgetenv("MOBILELAB_THEME") == "dark" ? Theme::Mode::Dark : Theme::Mode::Light);
    Proof w;
    w.show();
    QApplication::processEvents();
    QApplication::processEvents();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "glass-proof.png";
    QImage shot = w.grab().toImage();
    shot.save(out);
    // Magnified strip of the left end of the first capsule of every column for the written proof.
    QImage zoom(4 * 240 + 3 * 4, 240, QImage::Format_RGB32);
    zoom.fill(Qt::white);
    QPainter zp(&zoom);
    for (int col = 0; col < 4; ++col) {
        const QRect src(20 + col * 245 - 2, 68, 60, 60);
        zp.drawImage(QRect(col * (240 + 4), 0, 240, 240), shot.copy(src).scaled(240, 240, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    }
    zp.end();
    zoom.save(out.left(out.lastIndexOf('.')) + "-zoom.png");

    int failures = 0;
    auto matFor = [&](int col, const QString &name) -> GlassPanel * {
        for (auto *g : w.m_panels)
            if (g->objectName() == name + "-" + QString::number(col)) return g;
        return nullptr;
    };
    // Compare the rendered material of the capsule: blur vs full must differ only near the edges.
    QImage blur = matFor(1, "capsule")->materialImage(), full = matFor(2, "capsule")->materialImage();
    QImage fb = matFor(3, "capsule")->materialImage(), off = matFor(0, "capsule")->materialImage();
    int diffEdge = 0, diffCentre = 0;
    for (int y = 0; y < blur.height(); ++y)
        for (int x = 0; x < blur.width(); ++x) {
            const int d = qAbs(qRed(blur.pixel(x, y)) - qRed(full.pixel(x, y))) + qAbs(qGreen(blur.pixel(x, y)) - qGreen(full.pixel(x, y)));
            if (d < 12) continue;
            const int edgeDist = qMin(qMin(x, blur.width() - 1 - x), qMin(y, blur.height() - 1 - y));
            if (edgeDist < 16) ++diffEdge; else ++diffCentre;
        }
    std::printf("capsule blur vs full: %d px differ within 16px of the edge, %d px differ deeper inside\n", diffEdge, diffCentre);
    if (diffEdge < 200) { std::printf("FAIL: refraction changes almost nothing at the edges\n"); ++failures; }
    if (diffCentre > diffEdge / 4) { std::printf("FAIL: refraction leaks into the centre\n"); ++failures; }
    // Fallback: flat tint (few distinct colours in the middle row), unlike the backdrop dependent blur.
    QSet<QRgb> fbColours, blurColours;
    for (int x = fb.width() / 4; x < 3 * fb.width() / 4; ++x) { fbColours.insert(fb.pixel(x, fb.height() / 2)); blurColours.insert(blur.pixel(x, blur.height() / 2)); }
    std::printf("middle row colours: fallback %d, blur %d, off %d\n", int(fbColours.size()), int(blurColours.size()), int(off.pixelColor(off.width() / 2, off.height() / 2).rgb() != 0));
    if (fbColours.size() > 6) { std::printf("FAIL: fallback should be a flat tint\n"); ++failures; }
    if (blurColours.size() < 6) { std::printf("FAIL: blur should show the backdrop\n"); ++failures; }

    // Timing: recompute the material 50 times per shape (worst case, nothing cached).
    struct T { const char *name; int col; };
    for (auto t : {T{"capsule", 2}, T{"circle", 2}, T{"pill", 2}, T{"roundrect", 2}, T{"accent", 2}, T{"field", 2}, T{"capsule", 1}}) {
        auto *g = matFor(t.col, t.name);
        QElapsedTimer et;
        et.start();
        const int n = 50;
        for (int i = 0; i < n; ++i) g->materialImage();
        std::printf("material render %-10s level=%-5s %4dx%-3d : %.3f ms\n", t.name, Glass::levelName(g->effectiveLevel()).toUtf8().constData(),
                    g->shapeRect().width(), g->shapeRect().height(), et.nsecsElapsed() / 1e6 / n);
    }
    std::printf("%s\n", failures ? "glass proof FAILED" : "glass proof OK");
    return failures ? 1 : 0;
}
