#pragma once

#include "device/auth_manager.h"
#include "device/device_manager.h"
#include "ipc_server/ui_gateway.h"
#include "network/tcp_acceptor.h"
#include "storage/database.h"
#include "telemetry/telemetry_service.h"

#include <QObject>
#include <QJsonValue>
#include <QString>

class DeviceSession;

class DaemonApp : public QObject {
    Q_OBJECT

public:
    explicit DaemonApp(QObject *parent = nullptr);
    bool start();

private slots:
    void onEnvelopeReceived(
        DeviceSession *session, const DeviceEnvelope &envelope, const QString &remoteIp);

private:
    bool handleAuthHello(
        DeviceSession *session, const DeviceEnvelope &envelope, const QString &remoteIp);
    bool handleAuthProof(DeviceSession *session, const DeviceEnvelope &envelope);
    bool sendAuthChallenge(DeviceSession *session, const DeviceEnvelope &requestEnvelope,
        const QString &serverNonce);
    bool sendAuthResult(
        DeviceSession *session, const DeviceEnvelope &requestEnvelope, const QString &result);
    static bool parseJsonInt64(const QJsonValue &value, qint64 *out);

    bool isMarkerWritingEnabled() const;
    void writeTelemetryMarker(const DeviceEnvelope &envelope);

    TcpAcceptor acceptor_;
    Database database_;
    AuthManager authManager_;
    DeviceManager deviceManager_;
    UiGateway uiGateway_;
    TelemetryService telemetryService_;
    bool started_ = false;
    QString databasePath_;
    QString markerPath_;
    bool markerEnabledByEnv_ = false;
};
