#pragma once

#include "protocol/device_frame.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class Database;

class AuthManager {
public:
    enum class HelloDecision {
        SendChallenge,
        RegistrationRequired,
        Rejected,
    };

    struct HelloResult {
        HelloDecision decision = HelloDecision::Rejected;
        QString reason;
        QString serverNonce;
    };

    HelloResult handleAuthHello(const DeviceEnvelope &envelope, const QString &remoteIp,
        Database *database, QString *error = nullptr) const;
    bool verify(const QString &deviceId, const QString &serverNonce, const QString &clientNonce,
        qint64 ts, const QString &secret, const QByteArray &proof) const;

private:
    static bool constantTimeEquals(const QByteArray &lhs, const QByteArray &rhs);
};
