#include "device/auth_manager.h"

#include "security/hmac_helper.h"
#include "storage/database.h"

#include <QDateTime>
#include <QUuid>

namespace {
QString buildServerNonce() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

AuthManager::HelloResult AuthManager::handleAuthHello(
    const DeviceEnvelope &envelope, const QString &remoteIp, Database *database, QString *error) const {
    HelloResult result;
    if (!database) {
        if (error) {
            *error = QStringLiteral("database is null");
        }
        result.reason = QStringLiteral("internal_error");
        return result;
    }

    Database::StoredDevice stored;
    QString lookupError;
    if (!database->fetchDevice(envelope.deviceId, &stored, &lookupError)) {
        Database::PendingDeviceRequest pendingRequest;
        pendingRequest.deviceId = envelope.deviceId;
        pendingRequest.proposedName = envelope.payload.value(QStringLiteral("device_name")).toString();
        pendingRequest.firmwareVersion
            = envelope.payload.value(QStringLiteral("firmware_version")).toString();
        pendingRequest.hardwareModel
            = envelope.payload.value(QStringLiteral("hardware_model")).toString();
        pendingRequest.mac = envelope.payload.value(QStringLiteral("mac")).toString();
        pendingRequest.ip = remoteIp;
        pendingRequest.requestTime = envelope.ts > 0 ? envelope.ts : QDateTime::currentSecsSinceEpoch();
        pendingRequest.status = QStringLiteral("pending");

        if (!database->upsertPendingRequest(pendingRequest, error)) {
            result.reason = QStringLiteral("internal_error");
            return result;
        }

        result.decision = HelloDecision::RegistrationRequired;
        result.reason = QStringLiteral("registration_required");
        return result;
    }

    if (stored.info.status == DeviceLifecycleState::Disabled
        || stored.info.status == DeviceLifecycleState::Revoked) {
        result.decision = HelloDecision::Rejected;
        result.reason = QStringLiteral("rejected");
        return result;
    }

    if (stored.info.status != DeviceLifecycleState::Active) {
        result.decision = HelloDecision::RegistrationRequired;
        result.reason = QStringLiteral("registration_required");
        return result;
    }

    result.decision = HelloDecision::SendChallenge;
    result.reason = QStringLiteral("ok");
    result.serverNonce = buildServerNonce();
    return result;
}

bool AuthManager::verify(const QString &deviceId, const QString &serverNonce,
    const QString &clientNonce, qint64 ts, const QString &secret, const QByteArray &proof) const {
    return constantTimeEquals(HmacHelper::sign(deviceId, serverNonce, clientNonce, ts, secret), proof);
}

bool AuthManager::constantTimeEquals(const QByteArray &lhs, const QByteArray &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    quint8 diff = 0;
    for (int index = 0; index < lhs.size(); ++index) {
        diff |= static_cast<quint8>(lhs.at(index) ^ rhs.at(index));
    }
    return diff == 0;
}
