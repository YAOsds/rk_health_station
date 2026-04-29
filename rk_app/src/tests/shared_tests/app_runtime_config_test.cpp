#include "runtime_config/app_runtime_config_loader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QtTest/QTest>

class AppRuntimeConfigTest : public QObject {
    Q_OBJECT

private slots:
    void loadsBuiltInDefaults();
    void loadsJsonValuesFromFile();
    void environmentOverridesJsonValues();
    void preservesRelativeSocketPathFromEnvironmentOverride();
    void resolvesRelativePathsAgainstConfigDirectory();
    void rejectsInvalidEnumValues();
};

void AppRuntimeConfigTest::loadsBuiltInDefaults() {
    const auto result = loadAppRuntimeConfig(QString());
    QVERIFY(result.ok);
    QCOMPARE(result.config.video.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(result.config.video.devicePath, QStringLiteral("/dev/video11"));
    QCOMPARE(result.config.analysis.transport, QStringLiteral("shared_memory"));
}

void AppRuntimeConfigTest::loadsJsonValuesFromFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": { "camera_id": "rear_cam", "device_path": "/dev/video22" },
        "analysis": { "transport": "dmabuf" }
    })");
    file.close();

    const auto result = loadAppRuntimeConfig(file.fileName());
    QVERIFY(result.ok);
    QCOMPARE(result.config.video.cameraId, QStringLiteral("rear_cam"));
    QCOMPARE(result.config.analysis.transport, QStringLiteral("dmabuf"));
}

void AppRuntimeConfigTest::environmentOverridesJsonValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({ "analysis": { "transport": "shared_memory" } })");
    file.close();

    qputenv("RK_VIDEO_ANALYSIS_TRANSPORT", QByteArray("dmabuf"));
    const auto result = loadAppRuntimeConfig(file.fileName());
    QCOMPARE(result.config.analysis.transport, QStringLiteral("dmabuf"));
    QCOMPARE(result.origins.value(QStringLiteral("analysis.transport")), QStringLiteral("environment"));
    qunsetenv("RK_VIDEO_ANALYSIS_TRANSPORT");
}

void AppRuntimeConfigTest::preservesRelativeSocketPathFromEnvironmentOverride() {
    qputenv("RK_VIDEO_SOCKET_NAME", QByteArray("./run/custom/rk_video.sock"));

    const auto result = loadAppRuntimeConfig(QString());
    QVERIFY(result.ok);
    QCOMPARE(result.config.ipc.videoSocketPath, QStringLiteral("run/custom/rk_video.sock"));
    QCOMPARE(result.origins.value(QStringLiteral("ipc.video_socket")), QStringLiteral("environment"));

    qunsetenv("RK_VIDEO_SOCKET_NAME");
}

void AppRuntimeConfigTest::resolvesRelativePathsAgainstConfigDirectory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nested/run"))));

    QFile file(dir.filePath(QStringLiteral("nested/runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "ipc": { "video_socket": "./run/custom.sock" },
        "paths": { "database_path": "./data/test.sqlite" }
    })");
    file.close();

    const auto result = loadAppRuntimeConfig(file.fileName());
    QVERIFY(result.ok);
    QVERIFY(result.config.ipc.videoSocketPath.endsWith(QStringLiteral("/nested/run/custom.sock")));
    QVERIFY(result.config.paths.databasePath.endsWith(QStringLiteral("/nested/data/test.sqlite")));
}

void AppRuntimeConfigTest::rejectsInvalidEnumValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "analysis": { "transport": "not_valid" }
    })");
    file.close();

    const auto result = loadAppRuntimeConfig(file.fileName());
    QVERIFY(!result.ok);
    QVERIFY(!result.errors.isEmpty());
}

QTEST_MAIN(AppRuntimeConfigTest)

#include "app_runtime_config_test.moc"
