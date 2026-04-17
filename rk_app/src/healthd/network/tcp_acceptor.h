#pragma once

#include "protocol/device_frame.h"

#include <QObject>
#include <QSet>
#include <QTcpServer>

class DeviceSession;

class TcpAcceptor : public QObject {
    Q_OBJECT

public:
    explicit TcpAcceptor(QObject *parent = nullptr);
    bool start(quint16 port);

signals:
    void envelopeReceived(
        DeviceSession *session, const DeviceEnvelope &envelope, const QString &remoteIp);

private slots:
    void onNewConnection();

private:
    QTcpServer server_;
    QSet<DeviceSession *> sessions_;
};
