#include <QtTest>
#include <QAccessible>
#include <QApplication>
#include <QLabel>
#include <QRadioButton>
#include <QSettings>
#include "Theme.h"
#include "Widgets.h"

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
    void accentTableMeetsContrastForWhiteText() {
        QCOMPARE(Theme::accentCount, 7);
        for (int i = 0; i < Theme::accentCount; ++i) {
            const auto a = Theme::Accent(i);
            for (bool dark : {false, true}) {
                const QColor c = Theme::accentColor(a, dark);
                const QString what = QString("%1 %2: %3:1").arg(Theme::accentName(a), dark ? "dark" : "light").arg(contrast(Qt::white, c), 0, 'f', 2);
                QVERIFY2(contrast(Qt::white, c) >= 3.0, qPrintable(what));
                QCOMPARE(Theme::tokens(dark, a).accent, c);
                QCOMPARE(Theme::tokens(dark, a).accentText, QColor(Qt::white));
            }
            // the dark value is the brighter one, like Apple's system colours
            QVERIFY2(luminance(Theme::accentColor(a, true)) > luminance(Theme::accentColor(a, false)), qPrintable(Theme::accentName(a)));
        }
    }
    void blueIsTheDefaultAccentAndKeysRoundTrip() {
        QCOMPARE(Theme::accentColor(Theme::Accent::Blue, false).name(), QString("#0a7aff"));
        QCOMPARE(Theme::accentColor(Theme::Accent::Blue, true).name(), QString("#0a84ff"));
        QCOMPARE(Theme::tokens(false).accent, Theme::accentColor(Theme::Accent::Blue, false));
        const QStringList names{"Blue", "Purple", "Pink", "Red", "Orange", "Green", "Graphite"};
        for (int i = 0; i < names.size(); ++i) {
            QCOMPARE(Theme::accentName(Theme::Accent(i)), names[i]);
            QCOMPARE(Theme::accentFromKey(Theme::accentKey(Theme::Accent(i))), Theme::Accent(i));
        }
        QCOMPARE(Theme::accentFromKey("chartreuse"), Theme::Accent::Blue);
        QCOMPARE(Theme::accentFromKey(""), Theme::Accent::Blue);
    }
    void accentOnlyRecoloursTheTokensDerivedFromIt() {
        for (bool dark : {false, true}) {
            const Tokens base = Theme::tokens(dark);
            for (int i = 1; i < Theme::accentCount; ++i) {
                const Tokens k = Theme::tokens(dark, Theme::Accent(i));
                QVERIFY(k.accent != base.accent);
                QCOMPARE(k.selectionFocused, k.accent);
                QCOMPARE(k.running, k.accent);
                // status and syntax colours, the neutral selection and the surfaces stay put
                for (auto pick : {&Tokens::pass, &Tokens::fail, &Tokens::warn, &Tokens::breakpoint, &Tokens::folder, &Tokens::selection, &Tokens::window,
                                  &Tokens::editor, &Tokens::sidebar, &Tokens::text, &Tokens::synComment, &Tokens::synKeyword, &Tokens::synString,
                                  &Tokens::synNumber, &Tokens::synType, &Tokens::synIdentifier, &Tokens::synError, &Tokens::lineCurrent, &Tokens::lineError})
                    QCOMPARE(k.*pick, base.*pick);
            }
        }
    }
    void themeFollowsTheChosenAccent() {
        Theme &t = Theme::instance();
        t.resetAppearance();
        t.setMode(Theme::Mode::Dark);
        QSignalSpy changed(&t, &Theme::changed);
        t.setAccent(Theme::Accent::Pink);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(t.t().accent, Theme::accentColor(Theme::Accent::Pink, true));
        QCOMPARE(t.palette().color(QPalette::Highlight), t.t().accent);
        QCOMPARE(t.palette().color(QPalette::Link), t.t().accent);
        t.setMode(Theme::Mode::Light);
        QCOMPARE(t.t().accent, Theme::accentColor(Theme::Accent::Pink, false));
        t.setAccent(Theme::Accent::Pink);
        QCOMPARE(changed.count(), 2);   // setting the same accent again is not a change
        t.resetAppearance();
    }
    void settingsRoundTripThroughQSettings() {
        QTemporaryDir dir;
        const QString file = dir.filePath("t.ini");
        Theme &t = Theme::instance();
        t.resetAppearance();
        t.setMode(Theme::Mode::Dark);
        t.setAccent(Theme::Accent::Purple);
        t.setTextSize(Theme::TextSize::Large);
        t.setReducedMotion(true);
        {
            QSettings s(file, QSettings::IniFormat);
            t.save(s);
            s.sync();
        }
        {
            QSettings s(file, QSettings::IniFormat);
            QCOMPARE(s.value("appearance/accent").toString(), QString("purple"));
            QCOMPARE(s.value("appearance/textSize").toString(), QString("large"));
        }
        t.resetAppearance();
        t.setReducedMotion(false);
        QCOMPARE(t.accent(), Theme::Accent::Blue);
        QCOMPARE(t.textSize(), Theme::TextSize::Default);
        QSettings s(file, QSettings::IniFormat);
        t.load(s);
        QCOMPARE(t.mode(), Theme::Mode::Dark);
        QCOMPARE(t.accent(), Theme::Accent::Purple);
        QCOMPARE(t.textSize(), Theme::TextSize::Large);
        QVERIFY(t.reducedMotion());
        QCOMPARE(t.t().accent, Theme::accentColor(Theme::Accent::Purple, true));
        t.resetAppearance();
    }
    void invalidStoredValuesFallBackToTheDefaults() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("bad.ini"), QSettings::IniFormat);
        s.setValue("appearance/mode", "sepia");
        s.setValue("appearance/accent", "chartreuse");
        s.setValue("appearance/textSize", "huge");
        s.setValue("appearance/reducedMotion", "perhaps");
        Theme &t = Theme::instance();
        t.setAccent(Theme::Accent::Red);
        t.setTextSize(Theme::TextSize::Larger);
        t.load(s);
        QCOMPARE(t.mode(), Theme::Mode::System);
        QCOMPARE(t.accent(), Theme::Accent::Blue);
        QCOMPARE(t.textSize(), Theme::TextSize::Default);
        QVERIFY(!t.reducedMotion());
        QCOMPARE(t.t().accent, Theme::tokens(t.dark()).accent);
        s.setValue("appearance/accent", 42);
        s.setValue("appearance/textSize", QVariant());
        t.load(s);
        QCOMPARE(t.accent(), Theme::Accent::Blue);
        QCOMPARE(t.textSize(), Theme::TextSize::Default);
        t.resetAppearance();
    }
    void environmentHooksOverrideStoredValuesAndIgnoreJunk() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("env.ini"), QSettings::IniFormat);
        s.setValue("appearance/accent", "green");
        s.setValue("appearance/textSize", "small");
        Theme &t = Theme::instance();
        qputenv("MOBILELAB_ACCENT", "orange");
        qputenv("MOBILELAB_TEXT_SIZE", "larger");
        t.load(s);
        QCOMPARE(t.accent(), Theme::Accent::Orange);
        QCOMPARE(t.textSize(), Theme::TextSize::Larger);
        qputenv("MOBILELAB_ACCENT", "nonsense");
        qputenv("MOBILELAB_TEXT_SIZE", "");
        t.load(s);
        QCOMPARE(t.accent(), Theme::Accent::Green);   // an unknown value changes nothing
        QCOMPARE(t.textSize(), Theme::TextSize::Small);
        qunsetenv("MOBILELAB_ACCENT");
        qunsetenv("MOBILELAB_TEXT_SIZE");
        t.resetAppearance();
    }
    void defaultTextSizeLeavesTheFontsAlone() {
        Theme &t = Theme::instance();
        t.resetAppearance();
        QCOMPARE(t.textScale(), 1.0);
        QCOMPARE(QFontInfo(t.ui(13)).pixelSize(), 13);
        QCOMPARE(QFontInfo(t.ui(11.5)).pixelSize(), 12);
        QCOMPARE(QFontInfo(t.mono(12)).pixelSize(), 12);
        QCOMPARE(Theme::scaled(22), 22);
    }
    void textSizesScaleTheFontsByTheirFactor() {
        Theme &t = Theme::instance();
        const qreal factors[] = {0.92, 1.0, 1.1, 1.2};
        for (int i = 0; i < Theme::textSizeCount; ++i) {
            t.setTextSize(Theme::TextSize(i));
            QCOMPARE(t.textScale(), factors[i]);
            QCOMPARE(QFontInfo(t.ui(13)).pixelSize(), qRound(13 * factors[i]));
            QCOMPARE(QFontInfo(t.mono(12)).pixelSize(), qRound(12 * factors[i]));
        }
        t.resetAppearance();
    }
    void scaledMetricsAreMonotonicAndDefaultIsTheDesignValue() {
        struct M { const char *name; int (*fn)(); int base; };
        const M metrics[] = {{"toolbar", Metrics::toolbarHeight, 52}, {"control", Metrics::controlHeight, 28}, {"capsule", Metrics::capsuleHeight, 34},
                             {"jump bar", Metrics::jumpBarHeight, 28}, {"nav row", Metrics::navRow, 22}, {"gutter row", Metrics::gutterRow, 17},
                             {"filter bar", Metrics::filterBar, 34}, {"debug bar", Metrics::debugBar, 28}, {"footer", Metrics::footerHeight, 30}};
        Theme &t = Theme::instance();
        for (const auto &m : metrics) {
            int v[Theme::textSizeCount];
            for (int i = 0; i < Theme::textSizeCount; ++i) {
                t.setTextSize(Theme::TextSize(i));
                v[i] = m.fn();
            }
            QCOMPARE(v[int(Theme::TextSize::Default)], m.base);
            for (int i = 1; i < Theme::textSizeCount; ++i) QVERIFY2(v[i - 1] < v[i], m.name);
        }
        // any length, not just these: the step from one size to the next never shrinks it
        for (int px = 1; px <= 200; ++px) {
            int last = 0;
            for (int i = 0; i < Theme::textSizeCount; ++i) {
                t.setTextSize(Theme::TextSize(i));
                QVERIFY(Theme::scaled(px) >= last);
                last = Theme::scaled(px);
            }
        }
        // the icon-only tab bars and the panel geometry do not follow the text
        t.setTextSize(Theme::TextSize::Larger);
        QCOMPARE(Metrics::navTabBar, 44);
        QCOMPARE(Metrics::panelRadius, 14);
        t.resetAppearance();
    }
    void textSizeChangesAreAnnouncedOnceAndBeforeThePaintSignal() {
        Theme &t = Theme::instance();
        t.resetAppearance();
        QStringList order;
        connect(&t, &Theme::textScaleChanged, this, [&] { order << "scale"; });
        connect(&t, &Theme::changed, this, [&] { order << "changed"; });
        t.setTextSize(Theme::TextSize::Large);
        QCOMPARE(order, (QStringList{"scale", "changed"}));
        t.setTextSize(Theme::TextSize::Large);
        QCOMPARE(order.size(), 2);
        t.resetAppearance();
        QCOMPARE(order, (QStringList{"scale", "changed", "scale", "changed"}));
        t.resetAppearance();
        QCOMPARE(order.size(), 4);   // nothing to reset
        disconnect(&t, nullptr, this, nullptr);
    }
    void resetAppearanceTouchesOnlyAppearance() {
        Theme &t = Theme::instance();
        t.setMode(Theme::Mode::Dark);
        t.setAccent(Theme::Accent::Red);
        t.setTextSize(Theme::TextSize::Small);
        t.setReducedMotion(true);
        QSignalSpy motion(&t, &Theme::motionChanged);
        t.resetAppearance();
        QCOMPARE(t.mode(), Theme::Mode::System);
        QCOMPARE(t.accent(), Theme::Accent::Blue);
        QCOMPARE(t.textSize(), Theme::TextSize::Default);
        QVERIFY(!t.reducedMotion());
        QCOMPARE(motion.count(), 1);
        QCOMPARE(t.t().accent, Theme::tokens(t.dark()).accent);
        QCOMPARE(Metrics::panelRadius, 14);
    }
    void swatchRowIsARadioGroupWithNamesAndArrowKeys() {
        QStringList names;
        for (int i = 0; i < Theme::accentCount; ++i) names << Theme::accentName(Theme::Accent(i));
        SwatchRow row(names, [](int i) { return Theme::accentColor(Theme::Accent(i), false); }, "Accent color");
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));
        QCOMPARE(row.count(), 7);
        QCOMPARE(row.current(), 0);
        QCOMPARE(row.accessibleName(), QString("Accent color"));
        for (int i = 0; i < row.count(); ++i) {
            auto *b = row.swatch(i);
            QVERIFY(qobject_cast<QRadioButton *>(b));
            QCOMPARE(b->accessibleName(), "Accent color " + names[i]);
            QCOMPARE(b->isChecked(), i == 0);
            if (auto *iface = QAccessible::queryAccessibleInterface(b)) QCOMPARE(iface->role(), QAccessible::RadioButton);
        }
        QSignalSpy spy(&row, &SwatchRow::currentChanged);
        QTest::mouseClick(row.swatch(2), Qt::LeftButton, {}, row.swatch(2)->rect().center());
        QCOMPARE(row.current(), 2);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().first().toInt(), 2);
        row.swatch(2)->setFocus();
        QTest::keyClick(row.swatch(2), Qt::Key_Right);
        QCOMPARE(row.current(), 3);
        QVERIFY(row.swatch(3)->hasFocus());
        QTest::keyClick(row.swatch(3), Qt::Key_Left);
        QTest::keyClick(row.swatch(2), Qt::Key_Left);
        QCOMPARE(row.current(), 1);
        QCOMPARE(spy.count(), 4);
        QTest::keyClick(row.swatch(1), Qt::Key_Up);
        QTest::keyClick(row.swatch(0), Qt::Key_Left);   // the first one stays the first one
        QCOMPARE(row.current(), 0);
        QCOMPARE(spy.count(), 5);
        row.setCurrent(4);                               // programmatic changes are silent
        QCOMPARE(row.current(), 4);
        QCOMPARE(spy.count(), 5);
        QCOMPARE(row.findChildren<QLabel *>().value(0)->text(), QString("Orange"));
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
    qunsetenv("MOBILELAB_ACCENT");
    qunsetenv("MOBILELAB_TEXT_SIZE");
    qunsetenv("MOBILELAB_THEME");
    qunsetenv("MOBILELAB_REDUCED_MOTION");
    QApplication app(argc, argv);
    TstTheme t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_theme.moc"
