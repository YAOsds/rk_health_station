#include "device/device_manager.h"
#include "ipc_client/ui_ipc_client.h"
#include "ipc_server/ui_gateway.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QTest>

class HealthUiSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void connectsToBackend();
};

void HealthUiSmokeTest::connectsToBackend() {
    QVERIFY(QCoreApplication::instance() != nullptr);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME", tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    DeviceManager deviceManager;
    DeviceInfo info;
    info.deviceId = QStringLiteral("ui_smoke_001");
    info.deviceName = QStringLiteral("UI Smoke Watch");
    info.status = DeviceLifecycleState::Active;
    QVERIFY(deviceManager.updateMetadata(info));

    UiGateway gateway(&deviceManager);
    QVERIFY(gateway.start());

    UiIpcClient client;
    QVERIFY(client.connectToBackend());
    QVERIFY(client.isConnected());
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

QTEST_MAIN(HealthUiSmokeTest)

#include "health_ui_smoke_test.moc"
