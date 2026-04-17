#pragma once

#include "protocol/ipc_message.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class QLocalSocket;

class UiIpcClient : public QObject {
    Q_OBJECT

public:
    explicit UiIpcClient(QObject *parent = nullptr);

    bool connectToBackend();
    bool isConnected() const;
    void requestDeviceList();
    void requestDashboardSnapshot();
    void requestPendingDevices();
    void requestAlertsSnapshot();
    void requestHistorySeries(const QString &deviceId, qint64 fromTs, qint64 toTs);
    void approveDevice(
        const QString &deviceId, const QString &deviceName, const QString &secretHash);
    void rejectDevice(const QString &deviceId);
    void renameDevice(const QString &deviceId, const QString &deviceName);
    void setDeviceEnabled(const QString &deviceId, bool enabled);
    void resetDeviceSecret(const QString &deviceId, const QString &secretHash);

signals:
    void deviceListReceived(const QJsonArray &devices);
    void dashboardSnapshotReceived(const QJsonObject &snapshot);
    void pendingDevicesReceived(const QJsonArray &devices);
    void alertsSnapshotReceived(const QJsonArray &alerts);
    void historySeriesReceived(const QJsonObject &payload);
    void operationFinished(const QString &action, bool ok, const QJsonObject &payload);

private slots:
    void onReadyRead();

private:
    void sendRequest(const QString &action, const QString &reqId,
        const QJsonObject &payload = QJsonObject());
    void handleMessage(const IpcMessage &message);

    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
    int nextRequestId_ = 1;
};
