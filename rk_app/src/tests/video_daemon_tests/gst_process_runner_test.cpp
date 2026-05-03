#include "pipeline/gst_process_runner.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

class GstProcessRunnerTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsProcessThatExitsDuringStartupProbe();
    void stopsProcessWithSigintThenKillFallback();
    void reportsPreviewPipelineFailureToCallback();
};

void GstProcessRunnerTest::rejectsProcessThatExitsDuringStartupProbe() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write("#!/bin/sh\nexit 1\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    GstProcessRunner runner;
    QString error;
    QVERIFY(!runner.start(launcherPath, QProcess::SeparateChannels, {}, &error));
    QVERIFY(!error.isEmpty());
}

void GstProcessRunnerTest::stopsProcessWithSigintThenKillFallback() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "trap '' INT\n"
        "sleep 30\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    GstProcessRunner runner;
    QString error;
    QVERIFY(runner.start(launcherPath, QProcess::SeparateChannels, {}, &error));
    QVERIFY(runner.process() != nullptr);
    QVERIFY(runner.stop(&error));
    QVERIFY(runner.process() == nullptr);
}

void GstProcessRunnerTest::reportsPreviewPipelineFailureToCallback() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "sleep 1\n"
        "exit 7\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    GstProcessRunner runner;
    bool finishedCalled = false;
    int exitCode = 0;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    GstProcessRunner::Callbacks callbacks;
    callbacks.onFinished = [&](int code, QProcess::ExitStatus status) {
        finishedCalled = true;
        exitCode = code;
        exitStatus = status;
    };

    QString error;
    QVERIFY(runner.start(launcherPath, QProcess::SeparateChannels, callbacks, &error));
    QTRY_VERIFY_WITH_TIMEOUT(finishedCalled, 3000);
    QCOMPARE(exitCode, 7);
    QCOMPARE(exitStatus, QProcess::NormalExit);
}

QTEST_MAIN(GstProcessRunnerTest)
#include "gst_process_runner_test.moc"
