#pragma once

#include "protocol/device_frame.h"

#include <QByteArray>
#include <QObject>

class QTcpSocket;

class DeviceSession : public QObject {
    Q_OBJECT

public:
    enum class SessionAuthState {
        New,
        ChallengeSent,
        PendingApproval,
        Active,
        Rejected,
    };

    explicit DeviceSession(QTcpSocket *socket, QObject *parent = nullptr);
    SessionAuthState authState() const;
    void setAuthState(SessionAuthState state);
    QString authenticatedDeviceId() const;
    void setAuthenticatedDeviceId(const QString &deviceId);
    QString serverNonce() const;
    void setServerNonce(const QString &serverNonce);
    bool sendEnvelope(const DeviceEnvelope &envelope);

signals:
    void envelopeReceived(const DeviceEnvelope &envelope);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    bool tryExtractFrame(QByteArray *frameOut);
    void processReadBuffer();

    QTcpSocket *socket_ = nullptr;
    QByteArray readBuffer_;
    SessionAuthState authState_ = SessionAuthState::New;
    QString authenticatedDeviceId_;
    QString serverNonce_;
};
