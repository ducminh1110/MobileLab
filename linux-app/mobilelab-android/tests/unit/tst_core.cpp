#include <QtTest>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonObject>
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "ArtifactCollector.h"
#include "MatrixRun.h"
#include "ResourceScheduler.h"

class TstCore : public QObject {
    Q_OBJECT
private slots:
    void iniParsing() {
        const auto m = AndroidEmulator::parseIni("# c\nabi.type=x86_64\nimage.sysdir.1 = system-images/android-35/google_apis/x86_64/\n\nbad line\n");
        QCOMPARE(m.value("abi.type"), QString("x86_64"));
        QCOMPARE(AndroidEmulator::apiFromSysdir(m.value("image.sysdir.1")), QString("35"));
        QCOMPARE(AndroidEmulator::apiFromSysdir("android-34"), QString("34"));
        QCOMPARE(AndroidEmulator::apiFromSysdir("nothing"), QString());
        QCOMPARE(AndroidEmulator::normaliseAbi("arm64"), QString("arm64-v8a"));
        QCOMPARE(AndroidEmulator::normaliseAbi("x86_64"), QString("x86_64"));
    }

    void discoveryFromAvdHomeWithoutEmulatorBinary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir().mkpath(dir.path() + "/avd/A1.avd");
        QFile ini(dir.path() + "/avd/A1.ini");
        QVERIFY(ini.open(QIODevice::WriteOnly));
        ini.write(QString("avd.ini.encoding=UTF-8\npath=%1/avd/A1.avd\ntarget=android-33\n").arg(dir.path()).toUtf8());
        ini.close();
        QFile cfg(dir.path() + "/avd/A1.avd/config.ini");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("abi.type=arm64-v8a\nimage.sysdir.1=system-images/android-33/google_apis/arm64-v8a/\ntag.id=google_apis\nhw.device.name=pixel_6\n");
        cfg.close();
        qputenv("ANDROID_AVD_HOME", (dir.path() + "/avd").toUtf8());
        qputenv("ANDROID_HOME", (dir.path() + "/no-sdk").toUtf8());
        AndroidEmulator em;
        QVERIFY(!em.info().available);
        QVERIFY(em.discover());
        QCOMPARE(em.avds().size(), 1);
        const auto a = em.avds().first();
        QCOMPARE(a.name, QString("A1"));
        QCOMPARE(a.api, QString("33"));
        QCOMPARE(a.abi, QString("arm64-v8a"));
        QCOMPARE(a.tag, QString("google_apis"));
        QCOMPARE(a.device, QString("pixel_6"));
        AndroidRuntime rt;
        rt.setEmulator(&em);
        rt.probe();
        QCOMPARE(rt.targets().size(), 1);          // no invented placeholder targets
        QCOMPARE(rt.targets().first().stability, QString("fundamental"));
        QVERIFY(!rt.start("A1"));                   // no emulator binary: refuses honestly
        qunsetenv("ANDROID_AVD_HOME");
        qunsetenv("ANDROID_HOME");
    }

    void schedulerPrioritiesAndCapacity() {
        ResourceScheduler s;
        s.configure(1, 1024);                         // capacity 2 cost units
        QCOMPARE(s.capacity(), 2);
        QStringList started;
        connect(&s, &ResourceScheduler::jobStarted, this, [&](const QString &id) { started << id; });
        const QString low = s.submitExternal("a", "cmd", 1, 10);
        const QString high = s.submitExternal("b", "cmd", 1, 90);
        const QString third = s.submitExternal("c", "cmd", 1, 50);
        QTRY_COMPARE_WITH_TIMEOUT(started.size(), 2, 2000);
        QCOMPARE(started.at(0), high);                // highest priority first
        QCOMPARE(started.at(1), third);
        QCOMPARE(s.usedCost(), 2);
        QCOMPARE(s.queuedJobs().size(), 1);
        s.complete(high, true);                       // frees a slot, the queued job starts
        QTRY_COMPARE_WITH_TIMEOUT(started.size(), 3, 2000);
        QCOMPARE(started.at(2), low);
        s.cancel(third);
        s.complete(low, true);
        QCOMPARE(s.usedCost(), 0);
    }

    void schedulerLeaseCompletesApiJobs() {
        ResourceScheduler s;
        s.configure(1, 1024);
        s.setLeaseMs(60);
        QSignalSpy fin(&s, &ResourceScheduler::jobFinished);
        s.enqueue("t", "android-test");
        QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 3000);
        QCOMPARE(s.usedCost(), 0);
    }

    void matrixRecordRoundTrip() {
        MatrixRunRecord r;
        r.id = "matrix-1";
        r.started = QDateTime::currentDateTimeUtc();
        r.finished = r.started.addSecs(5);
        r.state = RunState::Failed;
        TargetResult t;
        t.avd = "P8"; t.api = "35"; t.abi = "x86_64"; t.state = RunState::Failed; t.logLine = 3; t.logLines = 12;
        StepResult ok; ok.name = "boot"; ok.state = RunState::Passed; ok.durationMs = 1234;
        StepResult bad; bad.name = "abi"; bad.state = RunState::Failed; bad.message = "expected"; bad.failLine = 7;
        t.steps = {ok, bad};
        r.targets.push_back(t);
        const auto back = MatrixRunRecord::fromJson(r.toJson(), "/tmp/x");
        QCOMPARE(back.id, r.id);
        QCOMPARE(back.state, RunState::Failed);
        QCOMPARE(back.targets.size(), 1);
        QCOMPARE(back.targets[0].steps.size(), 2);
        QCOMPARE(back.targets[0].steps[1].failLine, 7);
        QCOMPARE(back.targets[0].logLines, 12);
        QCOMPARE(back.failedSteps(), 1);
        QCOMPARE(back.totalSteps(), 2);
        QCOMPARE(back.logPath, QString("/tmp/x/run.log"));
    }

    void legacyAndInterruptedRuns() {
        // run.json written by the previous version of the app: only `passed` per target.
        QJsonObject legacy{{"id", "matrix-old"},
                           {"targets", QJsonArray{QJsonObject{{"avd", "a"}, {"passed", true}}, QJsonObject{{"avd", "b"}, {"passed", false}}}}};
        const auto r = MatrixRunRecord::fromJson(legacy, "/tmp/old");
        QCOMPARE(r.targets[0].state, RunState::Passed);
        QCOMPARE(r.targets[1].state, RunState::Failed);
        QCOMPARE(r.state, RunState::Failed);
        QJsonObject running{{"id", "matrix-x"}, {"state", "running"}, {"targets", QJsonArray{QJsonObject{{"avd", "a"}, {"state", "running"}}}}};
        const auto i = MatrixRunRecord::fromJson(running, "/tmp/i");
        QCOMPARE(i.state, RunState::Cancelled);          // never shows as running forever
        QCOMPARE(i.targets[0].state, RunState::Cancelled);
    }

    void apiPortDefaultsTo4100() {
        qunsetenv("MOBILELAB_ANDROID_API_PORT");
        QCOMPARE(ApiServer::configuredPort(), quint16(4100));
        qputenv("MOBILELAB_ANDROID_API_PORT", "4321");
        QCOMPARE(ApiServer::configuredPort(), quint16(4321));
        qputenv("MOBILELAB_ANDROID_API_PORT", "junk");
        QCOMPARE(ApiServer::configuredPort(), quint16(4100));
        qunsetenv("MOBILELAB_ANDROID_API_PORT");
    }

    void apiServerServesStatusOnEphemeralPort() {
        AndroidRuntime rt;
        ResourceScheduler sch;
        sch.configure(2, 2048);
        ApiServer api(&rt, &sch);
        qputenv("MOBILELAB_ANDROID_API_PORT", "0");
        QVERIFY(api.listen());
        QVERIFY(api.port() != 0);
        QTcpSocket sock;
        sock.connectToHost("127.0.0.1", api.port());
        QVERIFY(sock.waitForConnected(2000));
        sock.write("GET /status HTTP/1.1\r\nHost: x\r\n\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(sock.bytesAvailable() > 50, 3000);
        const QByteArray reply = sock.readAll();
        QVERIFY(reply.startsWith("HTTP/1.1 200"));
        QVERIFY(reply.contains("\"capacity\":4"));
        qunsetenv("MOBILELAB_ANDROID_API_PORT");
    }

    void artifactsAreUniqueAndCleanable() {
        QTemporaryDir dir;
        ArtifactCollector a(dir.path() + "/art");
        const QString d1 = a.beginRun("matrix-1"), d2 = a.beginRun("matrix-1");
        QVERIFY(d1 != d2);
        QVERIFY(a.writeText(d1, "x.txt", "hello"));
        QCOMPARE(a.sizeOnDisk(), qint64(5));
        QCOMPARE(a.cleanOlderThan(30), 0);
        QVERIFY(a.cleanOlderThan(0) >= 1);            // everything is older than "now"
        QCOMPARE(a.sizeOnDisk(), qint64(0));
    }
};

QTEST_MAIN(TstCore)
#include "tst_core.moc"
