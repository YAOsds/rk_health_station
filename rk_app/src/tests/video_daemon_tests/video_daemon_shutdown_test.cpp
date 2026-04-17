#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <signal.h>

namespace {

QString readTextFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

void killIfRunning(const QString &pidFilePath) {
    const qint64 pid = readTextFile(pidFilePath).trimmed().toLongLong();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
}

} // namespace

class VideoDaemonShutdownTest : public QObject {
    Q_OBJECT

private slots:
    void relaysSigtermIntoGracefulChildShutdown();
};

void VideoDaemonShutdownTest::relaysSigtermIntoGracefulChildShutdown() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString markerPath = tempDir.filePath(QStringLiteral("video-marker.log"));
    const QString pidFilePath = tempDir.filePath(QStringLiteral("video-child.pid"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));

    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write("#!/bin/sh\n"
                   "echo $$ > \"$RK_VIDEO_TEST_PIDFILE\"\n"
                   "echo started >> \"$RK_VIDEO_TEST_MARKER\"\n"
                   "trap 'echo stopped >> \"$RK_VIDEO_TEST_MARKER\"; exit 0' INT TERM\n"
                   "while :; do sleep 1; done\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcess daemon;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("RK_VIDEO_SOCKET_NAME"), tempDir.filePath(QStringLiteral("rk_video.sock")));
    env.insert(QStringLiteral("RK_VIDEO_GST_LAUNCH_BIN"), launcherPath);
    env.insert(QStringLiteral("RK_VIDEO_TEST_MARKER"), markerPath);
    env.insert(QStringLiteral("RK_VIDEO_TEST_PIDFILE"), pidFilePath);
    daemon.setProcessEnvironment(env);
    daemon.start(QStringLiteral(HEALTH_VIDEOD_BINARY), QStringList());
    QVERIFY2(daemon.waitForStarted(), qPrintable(daemon.errorString()));

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFilePath), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(readTextFile(markerPath).contains(QStringLiteral("started")), 3000);

    daemon.terminate();
    QVERIFY(daemon.waitForFinished(5000));
    QTRY_VERIFY_WITH_TIMEOUT(readTextFile(markerPath).contains(QStringLiteral("stopped")), 3000);

    killIfRunning(pidFilePath);
}

QTEST_MAIN(VideoDaemonShutdownTest)
#include "video_daemon_shutdown_test.moc"
