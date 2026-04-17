#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class HmacHelper {
public:
    static QByteArray sign(const QString &deviceId, const QString &serverNonce,
        const QString &clientNonce, qint64 ts, const QString &secret);
};
