#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

struct DeviceEnvelope {
    int ver = 1;
    QString type;
    qint64 seq = 0;
    qint64 ts = 0;
    QString deviceId;
    QJsonObject payload;
};

class DeviceFrameCodec {
public:
    // Wire format: 4-byte big-endian length prefix + compact UTF-8 JSON body.
    // Envelope contract fields: ver(int), type(string), seq(number), ts(number),
    // device_id(string), payload(object).
    // Returns empty QByteArray when required outbound fields are invalid.
    static QByteArray encode(const DeviceEnvelope &envelope);
    static bool decode(const QByteArray &frame, DeviceEnvelope *out);
};
