#include "protocol/device_frame.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace {
constexpr qint64 kMaxSafeJsonInteger = 9007199254740991LL; // 2^53 - 1

bool parseJsonInt64(const QJsonValue &value, qint64 *out) {
    if (!out || !value.isDouble()) {
        return false;
    }

    const double raw = value.toDouble();
    if (!qIsFinite(raw)
        || raw < -static_cast<double>(kMaxSafeJsonInteger)
        || raw > static_cast<double>(kMaxSafeJsonInteger)) {
        return false;
    }

    double integral = 0.0;
    if (std::modf(raw, &integral) != 0.0) {
        return false;
    }

    *out = static_cast<qint64>(integral);
    return true;
}

bool parseJsonInt(const QJsonValue &value, int *out) {
    if (!out || !value.isDouble()) {
        return false;
    }

    const double raw = value.toDouble();
    double integral = 0.0;
    if (!qIsFinite(raw)
        || std::modf(raw, &integral) != 0.0
        || raw < static_cast<double>(std::numeric_limits<int>::min())
        || raw > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }

    *out = static_cast<int>(integral);
    return true;
}

bool isSafeJsonInteger(qint64 value) {
    return value >= -kMaxSafeJsonInteger && value <= kMaxSafeJsonInteger;
}
} // namespace

QByteArray DeviceFrameCodec::encode(const DeviceEnvelope &envelope) {
    if (envelope.type.isEmpty() || envelope.deviceId.isEmpty()
        || !isSafeJsonInteger(envelope.seq) || !isSafeJsonInteger(envelope.ts)) {
        return {};
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("ver"), envelope.ver);
    obj.insert(QStringLiteral("type"), envelope.type);
    obj.insert(QStringLiteral("seq"), static_cast<double>(envelope.seq));
    obj.insert(QStringLiteral("ts"), static_cast<double>(envelope.ts));
    obj.insert(QStringLiteral("device_id"), envelope.deviceId);
    obj.insert(QStringLiteral("payload"), envelope.payload);

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);
    const quint32 len = static_cast<quint32>(body.size());

    QByteArray frame;
    frame.resize(4);
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >> 8) & 0xFF);
    frame[3] = static_cast<char>(len & 0xFF);
    frame.append(body);

    return frame;
}

bool DeviceFrameCodec::decode(const QByteArray &frame, DeviceEnvelope *out) {
    if (!out) {
        return false;
    }

    if (frame.size() < 4) {
        return false;
    }

    const quint32 len = (static_cast<quint8>(frame.at(0)) << 24)
        | (static_cast<quint8>(frame.at(1)) << 16)
        | (static_cast<quint8>(frame.at(2)) << 8)
        | static_cast<quint8>(frame.at(3));

    if (len > static_cast<quint32>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int bodyLen = static_cast<int>(len);
    if (bodyLen > std::numeric_limits<int>::max() - 4) {
        return false;
    }

    if (frame.size() < 4 + bodyLen) {
        return false;
    }

    const QByteArray body = frame.mid(4, bodyLen);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const QJsonValue verVal = obj.value(QStringLiteral("ver"));
    const QJsonValue typeVal = obj.value(QStringLiteral("type"));
    const QJsonValue seqVal = obj.value(QStringLiteral("seq"));
    const QJsonValue tsVal = obj.value(QStringLiteral("ts"));
    const QJsonValue deviceVal = obj.value(QStringLiteral("device_id"));
    const QJsonValue payloadVal = obj.value(QStringLiteral("payload"));

    if (verVal.isUndefined() || typeVal.isUndefined() || seqVal.isUndefined() || tsVal.isUndefined()
        || deviceVal.isUndefined() || payloadVal.isUndefined()) {
        return false;
    }

    DeviceEnvelope env;
    if (!parseJsonInt(verVal, &env.ver) || !typeVal.isString() || typeVal.toString().isEmpty()
        || !parseJsonInt64(seqVal, &env.seq) || !parseJsonInt64(tsVal, &env.ts)
        || !deviceVal.isString() || deviceVal.toString().isEmpty() || !payloadVal.isObject()) {
        return false;
    }

    env.type = typeVal.toString();
    env.deviceId = deviceVal.toString();
    env.payload = payloadVal.toObject();

    *out = env;
    return true;
}
