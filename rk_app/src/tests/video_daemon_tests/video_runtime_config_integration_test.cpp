#include "core/video_service.h"
#include "ipc/video_gateway.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest/QTest>

class VideoRuntimeConfigIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void initializesDefaultChannelFromJsonConfig();
    void resolvesVideoSocketPathFromJsonConfig();
};

void VideoRuntimeConfigIntegrationTest::initializesDefaultChannelFromJsonConfig() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": {
            "camera_id": "rear_cam",
            "device_path": "/dev/video77"
        }
    })");
    file.close();

    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(file.fileName());
    QVERIFY(loaded.ok);

    VideoService service(loaded.config, nullptr, nullptr, nullptr);
    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("rear_cam"));
    QCOMPARE(status.cameraId, QStringLiteral("rear_cam"));
    QCOMPARE(status.devicePath, QStringLiteral("/dev/video77"));
}

void VideoRuntimeConfigIntegrationTest::resolvesVideoSocketPathFromJsonConfig() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "ipc": {
            "video_socket": "./run/video_custom.sock"
        }
    })");
    file.close();

    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(file.fileName());
    QVERIFY(loaded.ok);

    VideoService service(loaded.config, nullptr, nullptr, nullptr);
    VideoGateway gateway(loaded.config.ipc.videoSocketPath, &service);
    QVERIFY(gateway.start());
    QVERIFY(QFile::exists(loaded.config.ipc.videoSocketPath));
}

QTEST_MAIN(VideoRuntimeConfigIntegrationTest)

#include "video_runtime_config_integration_test.moc"
