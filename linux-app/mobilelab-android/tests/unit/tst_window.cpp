// Behaviour of the real main window in offscreen mode: shortcuts, collapse controls, handles, persistence,
// Open Quickly, history, and a matrix run against the fake SDK fixture.
#include <QtTest>
#include <QApplication>
#include <QProcess>
#include <QSettings>
#include "AndroidRuntime.h"
#include "IconButton.h"
#include "MainWindow.h"
#include "MatrixExecutor.h"
#include "OpenQuickly.h"
#include "Theme.h"
#include "UiUtil.h"
#include "XcodeStyle.h"

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
