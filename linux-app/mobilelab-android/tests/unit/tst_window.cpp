// Behaviour of the real main window in offscreen mode: shortcuts, collapse controls, handles, persistence,
// Open Quickly, history, and a matrix run against the fake SDK fixture.
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>
#include "AndroidRuntime.h"
#include "EditorParts.h"
#include "IconButton.h"
#include "MainWindow.h"
#include "MatrixExecutor.h"
#include "OpenQuickly.h"
#include "Theme.h"
#include "UiUtil.h"
#include "Widgets.h"
#include "XcodeStyle.h"
#include "glass/Glass.h"

static QTemporaryDir *g_tmp = nullptr;

class TstWindow : public QObject {
    Q_OBJECT
    MainWindow *w = nullptr;

    void press(int key, Qt::KeyboardModifiers m) {
        if (QApplication::activeWindow() != w) { w->activateWindow(); QTest::qWaitForWindowActive(w); }   // a closed sheet leaves the window inactive
        QTest::keyClick(w, Qt::Key(key), m);
        QCoreApplication::processEvents();
    }
    PaneHandle *handleOf(PaneHost *h, int id) {
        for (auto *ph : h->findChildren<PaneHandle *>())
            if (ph->accessibleName().startsWith(h->spec(id).name)) return ph;
        return nullptr;
    }

private slots:
    void initTestCase() {
        Theme::ensureFonts();
        QApplication::setStyle(new XcodeStyle);
        QApplication::setFont(Theme::instance().ui(13));
        w = new MainWindow;
        w->setModalDialogsEnabled(false);
        w->resize(1400, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        w->activateWindow();
        QTest::qWaitForWindowActive(w);
        QTRY_VERIFY(w->loadedSettings());
    }
    void cleanupTestCase() { delete w; }

    void toolbarButtonsAreNamedAndExplainShortcuts() {
        int n = 0;
        for (auto *b : w->findChildren<IconButton *>()) {
            if (!b->isVisibleTo(w)) continue;
            ++n;
            QVERIFY2(!b->accessibleName().isEmpty(), qPrintable("unnamed icon button: " + b->iconName()));
            QVERIFY2(!b->toolTip().isEmpty(), qPrintable("no tooltip: " + b->iconName()));
        }
        QVERIFY(n >= 10);
        auto *nav = w->toolbar()->leftGroup()->buttons().first();
        QVERIFY2(nav->toolTip().contains("Ctrl+0"), qPrintable(nav->toolTip()));
        QVERIFY(w->toolbar()->rightGroup()->buttons()[1]->toolTip().contains("Ctrl+Alt+0"));
        QVERIFY(w->toolbar()->rightGroup()->buttons()[0]->toolTip().contains("Ctrl+Shift+Y"));
        QVERIFY(w->toolbar()->runGroup()->buttons()[1]->toolTip().contains("Ctrl+R"));
        QVERIFY(w->toolbar()->runGroup()->buttons()[0]->toolTip().contains("Ctrl+."));
        QVERIFY(!w->toolbar()->capsule()->accessibleName().isEmpty());
    }

    void shortcutsCollapseAndRestorePanels() {
        Theme::instance().setReducedMotion(true);
        QVERIFY(w->panes()->isShown(w->navigatorPane()));
        press(Qt::Key_0, Qt::ControlModifier);
        QVERIFY(!w->panes()->isShown(w->navigatorPane()));
        QCOMPARE(w->panes()->currentSize(w->navigatorPane()), 0);
        QVERIFY(!w->navigator()->isVisible());
        press(Qt::Key_0, Qt::ControlModifier);
        QVERIFY(w->panes()->isShown(w->navigatorPane()));
        QVERIFY(w->panes()->currentSize(w->navigatorPane()) >= 220);
        press(Qt::Key_0, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(!w->panes()->isShown(w->inspectorPane()));
        QVERIFY(!w->inspector()->isVisible());
        press(Qt::Key_0, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(w->panes()->isShown(w->inspectorPane()));
        QVERIFY(!w->editorPanes()->isShown(w->debugPane()));
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), 28);      // collapsed: only the status strip
        press(Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(w->editorPanes()->isShown(w->debugPane()));
        QVERIFY(w->editorPanes()->currentSize(w->debugPane()) >= 120);
        press(Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!w->editorPanes()->isShown(w->debugPane()));
        // toolbar buttons do the same
        auto *btn = w->toolbar()->leftGroup()->buttons().first();
        QTest::mouseClick(btn, Qt::LeftButton);
        QVERIFY(!w->panes()->isShown(w->navigatorPane()));
        QTest::mouseClick(btn, Qt::LeftButton);
        QVERIFY(w->panes()->isShown(w->navigatorPane()));
    }

    void animatedCollapseReachesItsTarget() {
        Theme::instance().setReducedMotion(false);
        press(Qt::Key_0, Qt::ControlModifier);
        QVERIFY(!w->panes()->isShown(w->navigatorPane()));            // logical state flips at once
        QTRY_COMPARE_WITH_TIMEOUT(w->panes()->currentSize(w->navigatorPane()), 0, 2000);
        press(Qt::Key_0, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(w->panes()->currentSize(w->navigatorPane()) >= 300, 2000);
        Theme::instance().setReducedMotion(true);
    }

    void navigatorTabsFollowCtrlDigits() {
        for (int i = 0; i < 6; ++i) {
            press(Qt::Key_1 + i, Qt::ControlModifier);
            QCOMPARE(w->navigator()->current(), i);
            QCOMPARE(w->navigator()->tabBar()->current(), i);
        }
        press(Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(w->navigator()->current(), 0);
    }

    void draggingPastTheMinimumSnapsClosedAndDoubleClickToggles() {
        Theme::instance().setReducedMotion(true);
        auto *host = w->panes();
        const int id = w->navigatorPane();
        auto *h = handleOf(host, id);
        QVERIFY(h);
        // inside the range: the size follows the mouse, clamped to [min, max]
        const int start = host->paneSize(id);
        QTest::mousePress(h, Qt::LeftButton, {}, h->rect().center());
        QTest::mouseMove(h, h->rect().center() + QPoint(60, 0));
        QCOMPARE(host->paneSize(id), qMin(start + 60, host->spec(id).maxSize));
        QTest::mouseMove(h, h->rect().center() + QPoint(-40, 0));   // relative to the press: start - 40
        QTest::mouseRelease(h, Qt::LeftButton, {}, h->rect().center() + QPoint(-40, 0));
        QVERIFY(host->isShown(id));
        QVERIFY(host->paneSize(id) >= host->spec(id).minSize);
        // past the minimum minus the snap distance: collapses
        h = handleOf(host, id);
        QTest::mousePress(h, Qt::LeftButton, {}, h->rect().center());
        QTest::mouseMove(h, h->rect().center() + QPoint(-260, 0));
        QVERIFY(!host->isShown(id));
        QTest::mouseRelease(h, Qt::LeftButton, {}, h->rect().center() + QPoint(-260, 0));
        QCOMPARE(host->currentSize(id), 0);
        w->showNavigator(true, false);
        QVERIFY(host->isShown(id));
        // double click on the divider collapses and expands
        h = handleOf(host, id);
        QVERIFY(h && h->isVisible());
        QTest::mouseDClick(h, Qt::LeftButton, {}, h->rect().center());
        QVERIFY(!host->isShown(id));
        w->showNavigator(true, false);
        // the vertical divider of the debug area behaves the same way
        w->showDebugArea(true, false);
        auto *dh = handleOf(w->editorPanes(), w->debugPane());
        QVERIFY(dh && dh->isVisible());
        QTest::mouseDClick(dh, Qt::LeftButton, {}, dh->rect().center());
        QVERIFY(!w->editorPanes()->isShown(w->debugPane()));
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), 28);
    }

    void keyboardOperationOfADivider() {
        auto *host = w->panes();
        const int id = w->navigatorPane();
        w->showNavigator(true, false);
        auto *h = handleOf(host, id);
        h->setFocus();
        const int before = host->paneSize(id);
        QTest::keyClick(h, Qt::Key_Right);
        QCOMPARE(host->paneSize(id), before + 16);
        QTest::keyClick(h, Qt::Key_Left);
        QCOMPARE(host->paneSize(id), before);
        QTest::keyClick(h, Qt::Key_Return);
        QVERIFY(!host->isShown(id));
        w->showNavigator(true, false);
    }

    void stateIsPersistedAndRestored() {
        w->showNavigator(true, false);
        w->panes()->setPaneSize(w->navigatorPane(), 333);
        w->showInspector(false, false);
        w->showDebugArea(true, false);
        w->selectNavigator(Navigator::Debug, false);
        Theme::instance().setMode(Theme::Mode::Dark);
        w->saveUiState();
        QSettings s;
        QCOMPARE(s.value("layout/navigatorWidth").toInt(), 333);
        QCOMPARE(s.value("layout/inspectorShown").toBool(), false);
        QCOMPARE(s.value("layout/debugShown").toBool(), true);
        QCOMPARE(s.value("navigator/tab").toInt(), int(Navigator::Debug));
        QCOMPARE(s.value("appearance/mode").toString(), QString("dark"));
        // a second window starts from what was saved
        MainWindow second;
        QVERIFY(second.loadedSettings());
        QCOMPARE(second.panes()->paneSize(second.navigatorPane()), 333);
        QVERIFY(!second.panes()->isShown(second.inspectorPane()));
        QVERIFY(second.editorPanes()->isShown(second.debugPane()));
        QCOMPARE(second.navigator()->current(), int(Navigator::Debug));
        Theme::instance().setMode(Theme::Mode::Light);
        w->showInspector(true, false);
        w->showDebugArea(false, false);
        w->selectNavigator(Navigator::Devices, false);
    }

    void openQuicklyFindsAndNavigates() {
        Location l;
        l.kind = Location::Container;
        l.id = "waydroid";
        w->navigate(l);
        press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        auto *q = qobject_cast<OpenQuickly *>(w->lastSheet());
        QVERIFY(q);
        QVERIFY(q->isVisible());
        q->setQuery("sett");
        QVERIFY(!q->results().isEmpty());
        QCOMPARE(q->results().first().command, QString("settings"));   // fuzzy "sett" -> Settings command
        q->setQuery("zzzzqqq");
        QVERIFY(q->results().isEmpty());
        q->setQuery("");
        delete q;
    }

    void historyWithBackAndForward() {
        Location a; a.kind = Location::Container; a.id = "waydroid";
        w->navigate(Location{});
        w->navigate(a);
        QCOMPARE(w->location().kind, Location::Container);
        press(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w->location().kind, Location::Welcome);
        press(Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w->location().kind, Location::Container);
        // also while a tree or the log view has the keyboard focus (they use the arrow keys themselves)
        w->selectNavigator(Navigator::Devices, true);
        QVERIFY(qobject_cast<NavTree *>(QApplication::focusWidget()));
        press(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w->location().kind, Location::Welcome);
        press(Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w->location().kind, Location::Container);
        w->navigate(Location{});
    }

    void textSizeRelayoutsTheWindowAtRunTime() {
        Theme &th = Theme::instance();
        th.resetAppearance();
        th.setReducedMotion(true);
        NavTree tree;
        tree.rebuild([](QStandardItemModel &m) {
            for (const char *id : {"a", "b", "c"}) {
                auto *it = new QStandardItem(id);
                it->setData(QString(id), NavRole::Id);
                m.appendRow(it);
            }
        });
        tree.resize(300, 200);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        auto *filter = w->navigator()->page(Navigator::Devices)->findChild<FilterBar *>();
        QVERIFY(filter);
        // the design values at the default size
        QTRY_COMPARE(w->toolbar()->height(), 52);
        QCOMPARE(w->toolbar()->capsule()->height(), 34 + 6);
        QCOMPARE(w->editor()->jumpBar()->height(), 28 + 6);
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), 28);
        QCOMPARE(filter->parentWidget()->height(), 34 + 8);
        QCOMPARE(tree.visualRect(tree.findId("a")).height(), 22);
        for (auto size : {Theme::TextSize::Larger, Theme::TextSize::Large, Theme::TextSize::Small, Theme::TextSize::Default}) {
            th.setTextSize(size);
            const QString what = Theme::textSizeName(size);
            QTRY_COMPARE_WITH_TIMEOUT(w->toolbar()->height(), Metrics::toolbarHeight(), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(w->toolbar()->capsule()->height(), Metrics::capsuleHeight() + 6, 2000);
            QTRY_COMPARE_WITH_TIMEOUT(w->editor()->jumpBar()->height(), Metrics::jumpBarHeight() + 6, 2000);
            QTRY_COMPARE_WITH_TIMEOUT(filter->parentWidget()->height(), Metrics::filterBar() + 8, 2000);
            QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), Metrics::debugBar());
            QCOMPARE(tree.visualRect(tree.findId("b")).height(), Metrics::navRow());
            QCOMPARE(tree.visualRect(tree.findId("b")).top(), Metrics::navRow());      // rows follow each other without gaps
            const QRect cap = w->toolbar()->capsule()->geometry();
            QVERIFY2(cap.top() >= 0 && cap.bottom() < w->toolbar()->height(), qPrintable(what + ": capsule leaves the toolbar"));
        }
        QCOMPARE(w->toolbar()->height(), 52);
        QCOMPARE(w->editor()->jumpBar()->height(), 28 + 6);
        QCOMPARE(tree.visualRect(tree.findId("a")).height(), 22);
        // the collapsed debug strip is the pane's size while it is closed
        th.setTextSize(Theme::TextSize::Larger);
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), Theme::scaled(28));
        w->showDebugArea(true, false);
        w->showDebugArea(false, false);
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), Theme::scaled(28));
        th.resetAppearance();
        th.setReducedMotion(true);
        QCOMPARE(w->editorPanes()->currentSize(w->debugPane()), 28);
    }

    void unfocusedSelectionStaysNeutralInsideTheIndentation() {
        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *edit = new QLineEdit(&host);
        NavTree tree(&host);
        lay->addWidget(edit);
        lay->addWidget(&tree, 1);
        tree.rebuild([](QStandardItemModel &m) {
            auto *parent = new QStandardItem("group");
            parent->setData(QString("g"), NavRole::Id);
            parent->setData(true, NavRole::Expand);
            auto *child = new QStandardItem("child");
            child->setData(QString("c"), NavRole::Id);
            parent->appendRow(child);
            m.appendRow(parent);
        });
        host.resize(300, 240);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        edit->setFocus();
        QVERIFY(tree.selectId("c"));
        QVERIFY(!tree.hasFocus());
        Theme::instance().setAccent(Theme::Accent::Purple);
        const QRect row = tree.visualRect(tree.findId("c"));
        const QImage img = tree.grab().toImage();
        const QColor under = img.pixelColor(20, row.center().y());
        QVERIFY2(under != Theme::accentColor(Theme::Accent::Purple, Theme::instance().dark()), qPrintable(under.name()));
        QCOMPARE(under, tk().selection);
        Theme::instance().resetAppearance();
        Theme::instance().setReducedMotion(true);
    }

    void settingsSheetAppearanceControlsFollowAndDriveTheTheme() {
        Theme &th = Theme::instance();
        th.resetAppearance();
        Glass::Settings::instance().setLevel(Glass::Level::Full);
        w->openSettings(SettingsSheet::General);
        auto *sheet = qobject_cast<SettingsSheet *>(w->lastSheet());
        QVERIFY(sheet);
        QCOMPARE(sheet->size(), QSize(600 + 40, 560 + 40));           // the sheet is as it was at the default text size
        auto *swatches = sheet->findChild<SwatchRow *>();
        QVERIFY(swatches);
        SegmentedControl *textSize = nullptr;
        for (auto *c : sheet->findChildren<SegmentedControl *>())
            if (c->accessibleName() == "Text size") textSize = c;
        QVERIFY(textSize);
        QCheckBox *motion = nullptr;
        for (auto *c : sheet->findChildren<QCheckBox *>())
            if (c->text() == "Reduce motion") motion = c;
        QVERIFY(motion);
        PillButton *reset = nullptr;
        for (auto *c : sheet->findChildren<PillButton *>())
            if (c->text() == "Reset Appearance") reset = c;
        QVERIFY(reset);
        QCOMPARE(swatches->current(), 0);
        QCOMPARE(textSize->current(), int(Theme::TextSize::Default));
        QVERIFY(!motion->isChecked());
        // the controls change the theme at once
        QTest::mouseClick(swatches->swatch(1), Qt::LeftButton, {}, swatches->swatch(1)->rect().center());
        QCOMPARE(th.accent(), Theme::Accent::Purple);
        QCOMPARE(tk().accent, Theme::accentColor(Theme::Accent::Purple, th.dark()));
        textSize->setFocus();
        QTest::keyClick(textSize, Qt::Key_Right);
        QCOMPARE(th.textSize(), Theme::TextSize::Large);
        QTest::keyClick(textSize, Qt::Key_Right);
        QCOMPARE(th.textSize(), Theme::TextSize::Larger);
        QVERIFY(sheet->width() > 600 + 40);                           // the sheet grows with the text
        motion->click();
        QVERIFY(th.reducedMotion());
        th.setMode(Theme::Mode::Dark);
        Glass::Settings::instance().setLevel(Glass::Level::Off);
        w->saveUiState();
        {
            QSettings s;
            QCOMPARE(s.value("appearance/accent").toString(), QString("purple"));
            QCOMPARE(s.value("appearance/textSize").toString(), QString("larger"));
            QCOMPARE(s.value("appearance/mode").toString(), QString("dark"));
            QCOMPARE(s.value("appearance/reducedMotion").toBool(), true);
        }
        // Reset Appearance restores all of it, and the controls show it
        reset->click();
        QCOMPARE(th.mode(), Theme::Mode::System);
        QCOMPARE(th.accent(), Theme::Accent::Blue);
        QCOMPARE(th.textSize(), Theme::TextSize::Default);
        QVERIFY(!th.reducedMotion());
        QCOMPARE(Glass::level(), Glass::Level::Full);
        QCOMPARE(swatches->current(), 0);
        QCOMPARE(textSize->current(), int(Theme::TextSize::Default));
        QVERIFY(!motion->isChecked());
        for (auto *r : sheet->findChildren<QRadioButton *>())
            if (r->accessibleName() == "Appearance System" || r->accessibleName() == "Liquid Glass Full (refracting Liquid Glass)") QVERIFY(r->isChecked());
        QCOMPARE(sheet->size(), QSize(600 + 40, 560 + 40));
        QCOMPARE(w->toolbar()->height(), 52);
        // layout sizes are not part of Appearance
        w->panes()->setPaneSize(w->navigatorPane(), 321);
        reset->click();
        QCOMPARE(w->panes()->paneSize(w->navigatorPane()), 321);
        delete sheet;
        th.setReducedMotion(true);
    }

    void welcomeShowsTheProjectVersion() {
        QCOMPARE(QString(MOBILELAB_VERSION), QString("4.1.0"));
        w->navigate(Location{});
        bool found = false;
        for (auto *l : w->editor()->welcome()->findChildren<QLabel *>())
            if (l->text() == "Android device lab, version 4.1.0") found = true;
        QVERIFY(found);
    }

    void fuzzyMatching() {
        QVERIFY(Ui::fuzzyScore("pix8", "Pixel_8_API_35") > 0);
        QVERIFY(Ui::fuzzyScore("p8a", "Pixel_8_API_35") > 0);
        QCOMPARE(Ui::fuzzyScore("zz", "Pixel_8_API_35"), -1);
        QVERIFY(Ui::fuzzyScore("pix", "Pixel_8") > Ui::fuzzyScore("pix", "Completely_Different_pixel"));   // prefix beats late match
        QVERIFY(Ui::fuzzyScore("abi", "abi") > Ui::fuzzyScore("abi", "a very big index"));                  // tight beats scattered
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MOBILELAB_REDUCED_MOTION", "1");
    QTemporaryDir tmp;
    g_tmp = &tmp;
    qputenv("MOBILELAB_SETTINGS_DIR", tmp.filePath("settings").toUtf8());
    qputenv("MOBILELAB_ANDROID_ARTIFACTS", tmp.filePath("artifacts").toUtf8());
    qputenv("MOBILELAB_ANDROID_API_PORT", "0");
    qputenv("ANDROID_HOME", tmp.filePath("no-sdk").toUtf8());
    qputenv("ANDROID_AVD_HOME", tmp.filePath("no-avd").toUtf8());
    qputenv("XDG_CACHE_HOME", tmp.filePath("cache").toUtf8());
    qputenv("XDG_DATA_HOME", tmp.filePath("data").toUtf8());
    QApplication app(argc, argv);
    QApplication::setOrganizationName("MobileLab");
    QApplication::setApplicationName("MobileLab Android");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.filePath("settings"));
    TstWindow t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_window.moc"
