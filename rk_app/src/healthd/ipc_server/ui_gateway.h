#pragma once

#include "alerts/alert_engine.h"
#include "protocol/ipc_message.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QJsonValue>

class DeviceManager;
class Database;
class QLocalServer;
class QLocalSocket;

class IpcCodec {
public:
    static QByteArray encode(const IpcMessage &message);
    static bool decode(const QByteArray &buffer, IpcMessage *out);
};

class UiGateway : public QObject {
    Q_OBJECT

public:
    static QString socketName();

    explicit UiGateway(
        DeviceManager *deviceManager, Database *database = nullptr, QObject *parent = nullptr);
    ~UiGateway() override;

    bool start();
    void stop();

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    IpcMessage routeMessage(const IpcMessage &message) const;
    IpcMessage buildDeviceListResponse(const QString &reqId) const;
    IpcMessage buildDashboardResponse(const QString &reqId) const;
    IpcMessage buildPendingDeviceResponse(const QString &reqId) const;
    IpcMessage buildAlertsSnapshotResponse(const QString &reqId) const;
    IpcMessage buildHistorySeriesResponse(const IpcMessage &message) const;
    IpcMessage handleApproveDevice(const IpcMessage &message) const;
    IpcMessage handleRejectDevice(const IpcMessage &message) const;
    IpcMessage handleRenameDevice(const IpcMessage &message) const;
    IpcMessage handleSetDeviceEnabled(const IpcMessage &message) const;
    IpcMessage handleResetDeviceSecret(const IpcMessage &message) const;
    IpcMessage buildErrorResponse(const QString &action, const QString &reqId,
        const QString &errorCode) const;
    static void appendDeviceJson(QJsonArray *devices, const QString &deviceId,
        const QString &deviceName, const QString &status, bool online, qint64 lastSeenAt,
        const QString &remoteIp);
    static bool readInt64Value(const QJsonValue &value, qint64 *out);

    DeviceManager *deviceManager_ = nullptr;
    Database *database_ = nullptr;
    QLocalServer *server_ = nullptr;
    QHash<QLocalSocket *, QByteArray> readBuffers_;
    mutable AlertEngine alertEngine_;
};
