// mobilelab-imagecheck FILE...: exits non-zero when a PNG is missing, tiny or blank (uniform colour).
#include <QGuiApplication>
#include <QImage>
#include <QSet>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
        QImage img(QString::fromLocal8Bit(argv[i]));
        if (img.isNull() || img.width() < 200 || img.height() < 150) {
            std::fprintf(stderr, "FAIL %s: missing or too small\n", argv[i]);
            ++bad;
            continue;
        }
        QSet<QRgb> colours;
        double sum = 0, sum2 = 0;
        const int n = img.width() * img.height();
        for (int y = 0; y < img.height(); y += 2)
            for (int x = 0; x < img.width(); x += 2) {
                const QRgb p = img.pixel(x, y);
                if (colours.size() < 4096) colours.insert(p);
                const double l = qGray(p);
                sum += l;
                sum2 += l * l;
            }
        const double cnt = n / 4.0, mean = sum / cnt, var = sum2 / cnt - mean * mean;
        if (colours.size() < 24 || var < 4.0) {
            std::fprintf(stderr, "FAIL %s: looks blank (%d colours, variance %.2f)\n", argv[i], int(colours.size()), var);
            ++bad;
        } else {
            std::printf("ok   %s %dx%d colours>=%d stddev=%.1f\n", argv[i], img.width(), img.height(), int(colours.size()), std::sqrt(var));
        }
    }
    return bad ? 1 : 0;
}
