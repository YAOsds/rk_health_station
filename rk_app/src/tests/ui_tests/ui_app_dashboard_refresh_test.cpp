#include "app/ui_app.h"
#include "protocol/ipc_message.h"

#include <QApplication>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest/QTest>

class UiAppDashboardRefreshTest : public QObject {
    Q_OBJECT

private slots:
    void requestsDashboardSnapshotAgainAfterTimerTick();
};

void UiAppDashboardRefreshTest::requestsDashboardSnapshotAgainAfterTimerTick() {
    QVERIFY(QCoreApplication::instance() != nullptr);

    QLocalServer server;
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString socketPath = tempDir.filePath(QStringLiteral("rk_health_station.sock"));
    qputenv("RK_HEALTH_STATION_SOCKET_NAME", socketPath.toUtf8());
    QVERIFY(server.listen(socketPath));

    UiApp app;
    QVERIFY(app.start());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    int dashboardRequests = 0;
    QObject::connect(socket, &QLocalSocket::readyRead, this, [&dashboardRequests, socket]() {
        const QList<QByteArray> frames = socket->readAll().split('\n');
        for (const QByteArray &frame : frames) {
            if (frame.trimmed().isEmpty()) {
                continue;
            }
            IpcMessage message;
            const QJsonDocument document = QJsonDocument::fromJson(frame);
            if (!document.isObject() || !ipcMessageFromJson(document.object(), &message)) {
                continue;
            }
            if (message.action == QStringLiteral("get_dashboard_snapshot")) {
                ++dashboardRequests;
            }
        }
    });

    QTRY_VERIFY_WITH_TIMEOUT(dashboardRequests >= 2, 4500);
    qunsetenv("RK_HEALTH_STATION_SOCKET_NAME");
}

QTEST_MAIN(UiAppDashboardRefreshTest)

#include "ui_app_dashboard_refresh_test.moc"
