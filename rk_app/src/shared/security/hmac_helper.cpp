#include "security/hmac_helper.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

QByteArray HmacHelper::sign(const QString &deviceId, const QString &serverNonce,
    const QString &clientNonce, qint64 ts, const QString &secret) {
    const QByteArray message = deviceId.toUtf8() + serverNonce.toUtf8() + clientNonce.toUtf8()
        + QByteArray::number(ts);
    return QMessageAuthenticationCode::hash(
               message, secret.toUtf8(), QCryptographicHash::Algorithm::Sha256)
        .toHex();
}
