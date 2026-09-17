#include "ResourceScheduler.h"

#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

class ResourceSchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void runsSubmittedCommand();
    void retriesFailedCommand();
    void prioritizesHigherPriorityWork();
    void cancellationDoesNotRetry();
};

void ResourceSchedulerTest::runsSubmittedCommand() {
    ResourceScheduler scheduler;
    QSignalSpy logs(&scheduler, &ResourceScheduler::logMessage);

    scheduler.submit("x86_64-dev", "printf scheduler-ok", 1, 50, 0);
    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.first().toString().contains("completed run-1")) {
                return true;
            }
        }
        return false;
    }(), 3000);

    bool sawOutput = false;
    for (const QList<QVariant> &entry : logs) {
        sawOutput |= entry.first().toString().contains("scheduler-ok");
    }
    QVERIFY(sawOutput);
}

void ResourceSchedulerTest::retriesFailedCommand() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString marker = temporaryDirectory.filePath("attempted");

    ResourceScheduler scheduler;
    QSignalSpy logs(&scheduler, &ResourceScheduler::logMessage);
    const QString command = QStringLiteral("if [ ! -f '%1' ]; then touch '%1'; exit 1; fi")
                                .arg(marker);
    scheduler.submit("arm64-dev", command, 1, 50, 1);

    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.first().toString().contains("completed run-1")) {
                return true;
            }
        }
        return false;
    }(), 3000);

    bool sawRetry = false;
    for (const QList<QVariant> &entry : logs) {
        sawRetry |= entry.first().toString().contains("retrying run-1 after attempt 1/2");
    }
    QVERIFY(sawRetry);
}

void ResourceSchedulerTest::prioritizesHigherPriorityWork() {
    ResourceScheduler scheduler;
    QSignalSpy logs(&scheduler, &ResourceScheduler::logMessage);
    scheduler.submit("x86_64-dev", "true", 1, 10, 0);
    scheduler.submit("x86_64-dev", "true", 1, 90, 0);

    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        int count = 0;
        for (const QList<QVariant> &entry : logs) {
            count += entry.first().toString().startsWith("started ");
        }
        return count >= 2;
    }(), 3000);

    QStringList started;
    for (const QList<QVariant> &entry : logs) {
        const QString message = entry.first().toString();
        if (message.startsWith("started ")) {
            started.append(message);
        }
    }
    QVERIFY(started.first().startsWith("started run-2:"));
    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        int count = 0;
        for (const QList<QVariant> &entry : logs) {
            count += entry.first().toString().startsWith("completed ");
        }
        return count == 2;
    }(), 3000);
}

void ResourceSchedulerTest::cancellationDoesNotRetry() {
    ResourceScheduler scheduler;
    QSignalSpy logs(&scheduler, &ResourceScheduler::logMessage);
    const QString id = scheduler.submit("x86_64-dev", "sleep 2", 1, 50, 3);

    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.first().toString().startsWith("started run-1:")) {
                return true;
            }
        }
        return false;
    }(), 1000);
    scheduler.cancel(id);
    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.first().toString() == "cancelled run-1") {
                return true;
            }
        }
        return false;
    }(), 1000);

    for (const QList<QVariant> &entry : logs) {
        QVERIFY(!entry.first().toString().startsWith("retrying run-1"));
    }
}

QTEST_MAIN(ResourceSchedulerTest)
#include "ResourceSchedulerTest.moc"
