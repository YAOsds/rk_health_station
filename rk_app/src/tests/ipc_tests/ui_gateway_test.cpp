#include "device/device_manager.h"
#include "ipc_server/ui_gateway.h"
#include "storage/database.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest/QTest>

class UiGatewayTest : public QObject {
    Q_OBJECT

private slots:
    void requestDeviceList();
    void approvePendingDeviceViaGateway();
    void requestAlertsSnapshot();
    void requestHistorySeries();
};

void UiGatewayTest::requestDeviceList() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME", tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    DeviceManager deviceManager;
    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_ipc_001");
    info.deviceName = QStringLiteral("IPC Watch");
    info.status = DeviceLifecycleState::Active;
    QVERIFY(deviceManager.updateMetadata(info));

    UiGateway gateway(&deviceManager);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("RK_HEALTH_STATION_SOCKET_NAME"));
    QVERIFY(socket.waitForConnected(3000));

    IpcMessage req {1, QStringLiteral("request"), QStringLiteral("get_device_list"),
        QStringLiteral("req-1"), true, {}};
    socket.write(IpcCodec::encode(req));
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);

    IpcMessage response;
    QVERIFY(IpcCodec::decode(socket.readAll(), &response));
    QCOMPARE(response.kind, QStringLiteral("response"));
    QCOMPARE(response.action, QStringLiteral("get_device_list"));
    QCOMPARE(response.reqId, QStringLiteral("req-1"));
    QVERIFY(response.ok);
    const QJsonArray devices = response.payload.value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 1);
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("device_id")).toString(),
        QStringLiteral("watch_ipc_001"));
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

void UiGatewayTest::approvePendingDeviceViaGateway() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME",
        tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("gateway_approve.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    Database::PendingDeviceRequest request;
    request.deviceId = QStringLiteral("watch_pending_ipc_001");
    request.proposedName = QStringLiteral("Pending IPC Watch");
    request.requestTime = 1713000500;
    QVERIFY2(database.upsertPendingRequest(request, &error), qPrintable(error));

    DeviceManager deviceManager(&database);
    UiGateway gateway(&deviceManager);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("RK_HEALTH_STATION_SOCKET_NAME"));
    QVERIFY(socket.waitForConnected(3000));

    IpcMessage req;
    req.kind = QStringLiteral("request");
    req.action = QStringLiteral("approve_device");
    req.reqId = QStringLiteral("req-approve-1");
    req.payload.insert(QStringLiteral("device_id"), QStringLiteral("watch_pending_ipc_001"));
    req.payload.insert(QStringLiteral("device_name"), QStringLiteral("Approved IPC Watch"));
    req.payload.insert(QStringLiteral("secret_hash"), QStringLiteral("secret-approved"));
    socket.write(IpcCodec::encode(req));
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);

    IpcMessage response;
    QVERIFY(IpcCodec::decode(socket.readAll(), &response));
    QCOMPARE(response.kind, QStringLiteral("response"));
    QCOMPARE(response.action, QStringLiteral("approve_device"));
    QVERIFY(response.ok);

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(QStringLiteral("watch_pending_ipc_001"), &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Active);
    QCOMPARE(stored.info.deviceName, QStringLiteral("Approved IPC Watch"));

    Database::PendingDeviceRequest pending;
    QVERIFY(!database.fetchPendingRequest(QStringLiteral("watch_pending_ipc_001"), &pending, &error));
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

void UiGatewayTest::requestAlertsSnapshot() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME",
        tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("gateway_alerts.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_alert_ipc_001");
    info.deviceName = QStringLiteral("Alert IPC Watch");
    info.status = DeviceLifecycleState::Active;

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = true;
    runtime.lastSeenAt = 1713008000;
    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("secret-alert"), -55, &error),
        qPrintable(error));

    Database::TelemetryRow row;
    row.sample.deviceId = info.deviceId;
    row.sample.sampleTime = 1713008000;
    row.heartRate = 48;
    QVERIFY2(database.insertTelemetrySample(row, &error), qPrintable(error));

    DeviceManager deviceManager(&database);
    QVERIFY(deviceManager.reloadFromDatabase(info.deviceId));
    UiGateway gateway(&deviceManager, &database);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("RK_HEALTH_STATION_SOCKET_NAME"));
    QVERIFY(socket.waitForConnected(3000));

    IpcMessage req;
    req.kind = QStringLiteral("request");
    req.action = QStringLiteral("get_alerts_snapshot");
    req.reqId = QStringLiteral("req-alerts-1");
    socket.write(IpcCodec::encode(req));
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);

    IpcMessage response;
    QVERIFY(IpcCodec::decode(socket.readAll(), &response));
    QCOMPARE(response.action, QStringLiteral("get_alerts_snapshot"));
    QVERIFY(response.ok);

    const QJsonArray alerts = response.payload.value(QStringLiteral("alerts")).toArray();
    QVERIFY(!alerts.isEmpty());
    QCOMPARE(alerts.at(0).toObject().value(QStringLiteral("alert_id")).toString(),
        QStringLiteral("heart_rate_low"));
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

void UiGatewayTest::requestHistorySeries() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME",
        tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("gateway_history.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    Database::TelemetryRow first;
    first.sample.deviceId = QStringLiteral("watch_history_ipc_001");
    first.sample.sampleTime = 1713009001;
    first.heartRate = 70;
    first.spo2 = 98.0;
    first.battery = 80;
    QVERIFY2(database.upsertTelemetryMinuteAgg(first, &error), qPrintable(error));

    Database::TelemetryRow second = first;
    second.sample.sampleTime = 1713009025;
    second.heartRate = 72;
    second.spo2 = 97.0;
    second.battery = 78;
    QVERIFY2(database.upsertTelemetryMinuteAgg(second, &error), qPrintable(error));

    DeviceManager deviceManager(&database);
    UiGateway gateway(&deviceManager, &database);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("RK_HEALTH_STATION_SOCKET_NAME"));
    QVERIFY(socket.waitForConnected(3000));

    IpcMessage req;
    req.kind = QStringLiteral("request");
    req.action = QStringLiteral("get_history_series");
    req.reqId = QStringLiteral("req-history-1");
    req.payload.insert(QStringLiteral("device_id"), QStringLiteral("watch_history_ipc_001"));
    req.payload.insert(QStringLiteral("from_ts"), static_cast<double>(1713009000));
    req.payload.insert(QStringLiteral("to_ts"), static_cast<double>(1713009059));
    socket.write(IpcCodec::encode(req));
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);

    IpcMessage response;
    QVERIFY(IpcCodec::decode(socket.readAll(), &response));
    QCOMPARE(response.action, QStringLiteral("get_history_series"));
    QVERIFY(response.ok);

    const QJsonArray series = response.payload.value(QStringLiteral("series")).toArray();
    QCOMPARE(series.size(), 1);
    const QJsonObject bucket = series.at(0).toObject();
    QCOMPARE(static_cast<qint64>(bucket.value(QStringLiteral("minute_ts")).toDouble()), qint64(1713009000));
    QCOMPARE(bucket.value(QStringLiteral("samples_total")).toInt(), 2);
    QCOMPARE(bucket.value(QStringLiteral("avg_heart_rate")).toDouble(), 71.0);
    QCOMPARE(bucket.value(QStringLiteral("min_spo2")).toDouble(), 97.0);
    QCOMPARE(bucket.value(QStringLiteral("avg_battery")).toDouble(), 79.0);
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

QTEST_MAIN(UiGatewayTest)

#include "ui_gateway_test.moc"
