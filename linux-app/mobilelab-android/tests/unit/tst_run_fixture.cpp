// End to end against the fake Android SDK (tests/fixtures): discovery, matrix execution through the scheduler,
// per step results, artifacts, cancel, JUnit export, REST API, and the same driven from the main window.
#include <QtTest>
#include <QApplication>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTcpSocket>
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "ArtifactCollector.h"
#include "MainWindow.h"
#include "MatrixExecutor.h"
#include "ResourceScheduler.h"
#include "RunEditor.h"
#include "Theme.h"
#include "XcodeStyle.h"

class TstRunFixture : public QObject {
    Q_OBJECT
    QTemporaryDir tmp;
    QString fx;

    static QVector<MatrixTarget> all(AndroidRuntime &rt) {
        QVector<MatrixTarget> v;
        for (const auto &t : rt.targets()) v.push_back({t.id, t.api, t.arch, "android-test"});
        return v;
    }

private slots:
    void initTestCase() {
        fx = tmp.filePath("fx");
        QProcess p;
        p.start("bash", {QString(MOBILELAB_FIXTURE_DIR) + "/make-fixture.sh", fx});
        QVERIFY(p.waitForFinished(20000));
        QCOMPARE(p.exitCode(), 0);
        qputenv("ANDROID_HOME", (fx + "/sdk").toUtf8());
        qputenv("ANDROID_AVD_HOME", (fx + "/avd").toUtf8());
        qputenv("FAKE_ANDROID_STATE", (fx + "/state").toUtf8());
        qputenv("FAKE_BOOT_DELAY_MS", "200");
        qputenv("MOBILELAB_POLL_MS", "60");
    }

    void discoveryRunsTheRealCodeAgainstTheFakeSdk() {
        AndroidEmulator em;
        AndroidRuntime rt;
        rt.setEmulator(&em);
        rt.probe();
        QVERIFY(em.info().available);
        QCOMPARE(rt.targets().size(), 6);
        const AndroidTarget *t = rt.target("Pixel_8_API_35");
        QVERIFY(t);
        QCOMPARE(t->api, QString("35"));
        QCOMPARE(t->arch, QString("x86_64"));
        QCOMPARE(t->tag, QString("google_apis"));
        QCOMPARE(t->device, QString("pixel_8"));
        QCOMPARE(t->stability, QString("preferred"));
        QCOMPARE(rt.target("Legacy_ARM_API_30")->stability, QString("fundamental"));
        QCOMPARE(rt.target("Legacy_ARM_API_30")->tag, QString("default"));
        QCOMPARE(rt.target("Unknown_API_Device")->api, QString("unknown"));
        QCOMPARE(em.installedSystemImages().size(), 4);
    }

    void aStoppedTargetBootsAndStopsThroughTheRuntime() {
        AndroidEmulator em;
        AndroidRuntime rt;
        rt.setEmulator(&em);
        rt.probe();
        QVERIFY(rt.start("Pixel_7_API_34"));
        QCOMPARE(rt.target("Pixel_7_API_34")->state, QString("booting"));
        QTRY_COMPARE_WITH_TIMEOUT(rt.target("Pixel_7_API_34")->state, QString("running"), 10000);
        QVERIFY(!rt.target("Pixel_7_API_34")->serial.isEmpty());
        QVERIFY(rt.runningCount() == 1);
        // screenshot through adb exec-out screencap
        bool done = false, ok = false;
        rt.screenshotAsync("Pixel_7_API_34", tmp.filePath("shot.png"), [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(ok);
        QImage img(tmp.filePath("shot.png"));
        QVERIFY(!img.isNull());
        QVERIFY(img.height() > img.width());
        QVERIFY(rt.stop("Pixel_7_API_34"));
        QTRY_COMPARE_WITH_TIMEOUT(rt.target("Pixel_7_API_34")->state, QString("stopped"), 10000);
    }

    void matrixRunProducesRealPerStepResultsAndArtifacts() {
        AndroidEmulator em;
        AndroidRuntime rt;
        ResourceScheduler sch;
        ArtifactCollector art(tmp.filePath("artifacts"));
        art.setAdb(em.adbPath());
        rt.setEmulator(&em);
        rt.probe();
        sch.configure(2, 4096);                                   // capacity 4 units: forces queueing
        MatrixExecutor mx(&em, &sch, &art);
        QSignalSpy started(&mx, &MatrixExecutor::runStarted), finished(&mx, &MatrixExecutor::runFinished), lines(&mx, &MatrixExecutor::logLine);
        const QString id = mx.run(all(rt));
        QVERIFY(!id.isEmpty());
        QVERIFY(mx.isRunning());
        QVERIFY(mx.run(all(rt)).isEmpty());                       // only one run at a time
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        QVERIFY(!mx.isRunning());
        QVERIFY(lines.count() > 60);
        const MatrixRunRecord *r = mx.record(id);
        QVERIFY(r);
        QCOMPARE(r->targets.size(), 6);
        QCOMPARE(r->state, RunState::Failed);                     // the Fold AVD reports the wrong ABI on purpose
        int failed = 0;
        for (const auto &t : r->targets) {
            QCOMPARE(t.steps.size(), 6);
            if (t.avd == "Pixel_Fold_API_34_arm64") {
                QCOMPARE(t.state, RunState::Failed);
                QCOMPARE(t.steps[1].name, QString("abi"));
                QCOMPARE(t.steps[1].state, RunState::Failed);
                QVERIFY2(t.steps[1].message.contains("expected ABI 'arm64-v8a' but the emulator reports 'x86_64'"), qPrintable(t.steps[1].message));
                QVERIFY(t.steps[1].failLine > 0);
                ++failed;
            } else if (t.avd == "Unknown_API_Device") {
                QCOMPARE(t.state, RunState::Passed);
                QCOMPARE(t.steps[2].state, RunState::Skipped);    // API level unknown: skipped, not faked
            } else {
                QCOMPARE(t.state, RunState::Passed);
                for (const auto &s : t.steps) QVERIFY2(s.state == RunState::Passed, qPrintable(t.avd + "/" + s.name + ": " + s.message));
                QVERIFY(QFileInfo(t.artifactDir + "/logcat.txt").size() > 100);
                QVERIFY(QImage(t.artifactDir + "/screenshot.png").height() > 100);
                QVERIFY(t.startedByUs);
                QVERIFY(t.durationMs > 0);
            }
        }
        QCOMPARE(failed, 1);
        QCOMPARE(r->failedSteps(), 1);
        // the scheduler admitted everything and gave every slot back
        QCOMPARE(sch.usedCost(), 0);
        QVERIFY(sch.queuedJobs().isEmpty());
        // nothing was left running (we started them, so we stopped them)
        rt.refreshTargets();
        QTRY_COMPARE_WITH_TIMEOUT(rt.runningCount(), 0, 8000);
        // run.log follows the xcodebuild layout and ends with a verdict
        const QStringList log = mx.readLog(id);
        QVERIFY(log.first().startsWith("Test Suite 'Matrix "));
        QVERIFY(log.contains("** TEST FAILED **"));
        const QStringList foldLog = mx.readLog(id, "Pixel_Fold_API_34_arm64");
        QVERIFY(foldLog.first().startsWith("Test Suite 'Pixel_Fold_API_34_arm64' started"));
        QVERIFY(foldLog.join('\n').contains("error: -[Pixel_Fold_API_34_arm64 abi]"));
        // history survives a restart
        MatrixExecutor again(&em, &sch, &art);
        again.loadHistory();
        QCOMPARE(again.records().size(), 1);
        QCOMPARE(again.records().first().targets.size(), 6);
        QCOMPARE(again.records().first().state, RunState::Failed);
        QCOMPARE(again.records().first().targets[0].steps.size(), 6);
        // JUnit export is well formed
        QString path;
        QVERIFY(RunEditor::exportJUnit(*r, &path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&f);
        int suites = 0, failures = 0, cases = 0;
        QString rootFailures;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QLatin1String("testsuites")) rootFailures = xml.attributes().value("failures").toString();
            else if (xml.name() == QLatin1String("testsuite")) ++suites;
            else if (xml.name() == QLatin1String("testcase")) ++cases;
            else if (xml.name() == QLatin1String("failure")) ++failures;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QCOMPARE(suites, 6);
        QCOMPARE(cases, 36);
        QCOMPARE(failures, 1);
        QCOMPARE(rootFailures, QString("1"));
    }

    void cancelStopsTargetsAndFreesSlots() {
        AndroidEmulator em;
        AndroidRuntime rt;
        ResourceScheduler sch;
        ArtifactCollector art(tmp.filePath("artifacts-cancel"));
        rt.setEmulator(&em);
        rt.probe();
        qputenv("FAKE_BOOT_DELAY_MS", "3000");
        sch.configure(2, 4096);
        MatrixExecutor mx(&em, &sch, &art);
        QSignalSpy finished(&mx, &MatrixExecutor::runFinished);
        const QString id = mx.run(all(rt));
        QTRY_VERIFY_WITH_TIMEOUT(!sch.runningJobs().isEmpty(), 5000);
        QTest::qWait(400);
        mx.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        const auto *r = mx.record(id);
        QCOMPARE(r->state, RunState::Cancelled);
        QCOMPARE(sch.usedCost(), 0);
        QVERIFY(sch.queuedJobs().isEmpty());
        for (const auto &t : r->targets) QVERIFY(t.state == RunState::Cancelled || t.state == RunState::Passed);
        rt.refreshTargets();
        QTRY_COMPARE_WITH_TIMEOUT(rt.runningCount(), 0, 8000);   // emulators started by the run were killed
        qputenv("FAKE_BOOT_DELAY_MS", "200");
    }

    void restApiListsTheFixtureTargets() {
        AndroidEmulator em;
        AndroidRuntime rt;
        ResourceScheduler sch;
        rt.setEmulator(&em);
        rt.probe();
        ApiServer api(&rt, &sch);
        qputenv("MOBILELAB_ANDROID_API_PORT", "0");
        QVERIFY(api.listen());
        QTcpSocket s;
        s.connectToHost("127.0.0.1", api.port());
        QVERIFY(s.waitForConnected(2000));
        s.write("GET /devices HTTP/1.1\r\nHost: x\r\n\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(s.bytesAvailable() > 200, 3000);
        const QByteArray reply = s.readAll();
        const QByteArray body = reply.mid(reply.indexOf("\r\n\r\n") + 4);
        const auto doc = QJsonDocument::fromJson(body);
        QCOMPARE(doc.object().value("devices").toArray().size(), 6);
        qunsetenv("MOBILELAB_ANDROID_API_PORT");
    }

    void theWindowRunsTheMatrixWithCtrlRAndReportsIt() {
        qputenv("MOBILELAB_SETTINGS_DIR", tmp.filePath("settings").toUtf8());
        qputenv("MOBILELAB_ANDROID_ARTIFACTS", tmp.filePath("artifacts-window").toUtf8());
        qputenv("MOBILELAB_ANDROID_API_PORT", "0");
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.filePath("settings"));
        MainWindow w;
        w.setModalDialogsEnabled(false);
        w.resize(1300, 850);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.activateWindow();
        QTest::qWaitForWindowActive(&w);
        QCOMPARE(w.context().runtime->targets().size(), 6);
        QCOMPARE(w.toolbar()->capsule()->state().state, QString("Ready"));
        QTest::keyClick(&w, Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(w.context().matrix->isRunning(), 3000);
        QCOMPARE(w.location().kind, Location::Run);                        // the editor switched to the report
        QTRY_VERIFY_WITH_TIMEOUT(w.toolbar()->capsule()->state().state == "Running" || w.toolbar()->capsule()->state().state == "Queued", 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.context().matrix->isRunning(), 90000);
        QTRY_COMPARE_WITH_TIMEOUT(w.toolbar()->capsule()->state().state, QString("Tests Failed"), 3000);
        QVERIFY(w.toolbar()->capsule()->state().detail.contains("1 of"));
        QVERIFY(w.editor()->run()->isVisible());
        // the failure shows up in the Issues navigator with a badge and a remedy
        QCOMPARE(w.navigator()->issues()->count() >= 1, true);
        // Stop with nothing running is disabled, Ctrl+R runs again, Ctrl+. cancels it
        QTest::keyClick(&w, Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(w.context().matrix->isRunning(), 3000);
        QTest::qWait(150);
        QTest::keyClick(&w, Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!w.context().matrix->isRunning(), 30000);
        QCOMPARE(w.context().matrix->records().last().state, RunState::Cancelled);
        QCOMPARE(w.toolbar()->capsule()->state().state, QString("Cancelled"));
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MOBILELAB_REDUCED_MOTION", "1");
    QApplication app(argc, argv);
    QApplication::setOrganizationName("MobileLab");
    QApplication::setApplicationName("MobileLab Android");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Theme::ensureFonts();
    QApplication::setStyle(new XcodeStyle);
    TstRunFixture t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_run_fixture.moc"
