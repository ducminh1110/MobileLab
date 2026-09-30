#include <QtTest>
#include <QApplication>
#include "Theme.h"

static double luminance(const QColor &c) {
    auto f = [](double v) { v /= 255.0; return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
}
static double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

class TstTheme : public QObject {
    Q_OBJECT
private slots:
    void lightTokensMatchTheSpec() {
        const Tokens k = Theme::tokens(false);
        QCOMPARE(k.window.name(), QString("#f5f5f7"));
        QCOMPARE(k.sidebar.name(), QString("#f3f5f7"));
        QCOMPARE(k.editor.name(), QString("#ffffff"));
        QCOMPARE(k.gutter.name(), QString("#fbfbfc"));
        QCOMPARE(k.gutterText.name(), QString("#b4b6ba"));
        QCOMPARE(k.divider.name(), QString("#e4e4e8"));
        QCOMPARE(k.text.name(), QString("#1d1d1f"));
        QCOMPARE(k.textSecondary.name(), QString("#6c6c72"));
        QCOMPARE(k.textTertiary.name(), QString("#a1a1a8"));
        QCOMPARE(k.accent.name(), QString("#0a7aff"));
        QCOMPARE(k.selection.name(), QString("#dcdde1"));
        QCOMPARE(k.capsule.name(), QString("#ffffff"));
        QCOMPARE(k.folder.name(), QString("#5cb0f5"));
        QCOMPARE(k.field.name(), QString("#efeff1"));
        QCOMPARE(k.debugBar.name(), QString("#f2f2f4"));
        QCOMPARE(k.console.name(), QString("#eaf1e8"));
        QCOMPARE(k.consoleFooter.name(), QString("#dbe7d9"));
        QCOMPARE(k.lineCurrent.name(), QString("#e5f7e8"));
        QCOMPARE(k.lineError.name(), QString("#fdeceb"));
        QCOMPARE(k.pass.name(), QString("#30b356"));
        QCOMPARE(k.fail.name(), QString("#ff3b30"));
        QCOMPARE(k.warn.name(), QString("#ff9f0a"));
        QCOMPARE(k.breakpoint.name(), QString("#097dfe"));
        QCOMPARE(k.synComment.name(), QString("#5d6c79"));
        QCOMPARE(k.synKeyword.name(), QString("#ad3da4"));
        QCOMPARE(k.synString.name(), QString("#d12f1b"));
        QCOMPARE(k.synNumber.name(), QString("#272ad8"));
        QCOMPARE(k.synType.name(), QString("#703daa"));
        QCOMPARE(k.synIdentifier.name(), QString("#326d74"));
        QCOMPARE(k.selectionFocused, k.accent);
        QCOMPARE(k.running, k.accent);
        QVERIFY(!k.dark);
    }
    void darkTokensMatchTheSpec() {
        const Tokens k = Theme::tokens(true);
        QCOMPARE(k.window.name(), QString("#2a2a2d"));
        QCOMPARE(k.sidebar.name(), QString("#262629"));
        QCOMPARE(k.editor.name(), QString("#1f1f24"));
        QCOMPARE(k.gutterText.name(), QString("#5f6068"));
        QCOMPARE(k.divider.name(), QString("#3a3a3f"));
        QCOMPARE(k.text.name(), QString("#ececee"));
        QCOMPARE(k.textSecondary.name(), QString("#a0a0a8"));
        QCOMPARE(k.accent.name(), QString("#0a84ff"));
        QCOMPARE(k.selection.name(), QString("#3c3c42"));
        QCOMPARE(k.capsule.name(), QString("#3b3b3f"));
        QCOMPARE(k.field.name(), QString("#333338"));
        QCOMPARE(k.debugBar.name(), QString("#2c2c30"));
        QCOMPARE(k.console.name(), QString("#1d2a22"));
        QCOMPARE(k.consoleFooter.name(), QString("#17211b"));
        QCOMPARE(k.lineCurrent.name(), QString("#26372c"));
        QCOMPARE(k.lineError.name(), QString("#3a2426"));
        QCOMPARE(k.pass.name(), QString("#32d15b"));
        QCOMPARE(k.fail.name(), QString("#ff453a"));
        QCOMPARE(k.synKeyword.name(), QString("#fc5fa3"));
        QCOMPARE(k.synNumber.name(), QString("#d0bf69"));
        QVERIFY(k.dark);
        // "no black backgrounds, no pure white text"
        QVERIFY(k.window != QColor(Qt::black) && k.editor != QColor(Qt::black));
        QVERIFY(k.text != QColor(Qt::white));
    }
    void contrastMeetsWcagAA() {
        for (bool dark : {false, true}) {
            const Tokens k = Theme::tokens(dark);
            const QString m = dark ? "dark" : "light";
            QVERIFY2(contrast(k.text, k.editor) >= 4.5, qPrintable("text/editor " + m));
            QVERIFY2(contrast(k.text, k.sidebar) >= 4.5, qPrintable("text/sidebar " + m));
            QVERIFY2(contrast(k.textSecondary, k.editor) >= 4.5, qPrintable("secondary/editor " + m));
            QVERIFY2(contrast(k.textSecondary, k.sidebar) >= 4.5, qPrintable("secondary/sidebar " + m));
            QVERIFY2(contrast(k.text, k.console) >= 4.5, qPrintable("text/console " + m));
            QVERIFY2(contrast(k.accentText, k.selectionFocused) >= 3.5, qPrintable("selected row " + m));
            QVERIFY2(contrast(k.text, k.lineCurrent) >= 4.5, qPrintable("text/current line " + m));
            QVERIFY2(contrast(k.text, k.lineError) >= 4.5, qPrintable("text/error line " + m));
            QVERIFY2(contrast(k.synIdentifier, k.editor) >= 4.5, qPrintable("identifier " + m));
            QVERIFY2(contrast(k.synString, k.editor) >= 4.0, qPrintable("string " + m));
        }
    }
    void themeFollowsModeAndOverride() {
        Theme &t = Theme::instance();
        t.setMode(Theme::Mode::Dark);
        QVERIFY(t.dark());
        QCOMPARE(t.t().editor.name(), QString("#1f1f24"));
        t.setMode(Theme::Mode::Light);
        QVERIFY(!t.dark());
        t.setMode(Theme::Mode::System);
        t.setSystemDark(true);
        QVERIFY(t.dark());
        t.setSystemDark(false);
        QVERIFY(!t.dark());
        QSignalSpy spy(&t, &Theme::changed);
        t.setMode(Theme::Mode::Dark);
        QCOMPARE(spy.count(), 1);
        t.setMode(Theme::Mode::Light);
    }
    void embeddedFontsResolve() {
        QVERIFY(Theme::ensureFonts());
        const QFontInfo ui(Theme::instance().ui(13));
        QCOMPARE(ui.family(), QString("Inter"));
        QCOMPARE(ui.pixelSize(), 13);
        const QFontInfo mono(Theme::instance().mono(12));
        QCOMPARE(mono.family(), QString("JetBrains Mono"));
        QVERIFY(QFontInfo(Theme::instance().ui(13, QFont::Bold)).bold());
    }
    void paletteUsesTokens() {
        Theme &t = Theme::instance();
        t.setMode(Theme::Mode::Light);
        const QPalette p = t.palette();
        QCOMPARE(p.color(QPalette::Highlight), t.t().accent);
        QCOMPARE(p.color(QPalette::Text), t.t().text);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TstTheme t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_theme.moc"
