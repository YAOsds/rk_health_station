#include "protocol/ipc_message.h"

#include <QJsonValue>

#include <cmath>
#include <limits>

QJsonObject ipcMessageToJson(const IpcMessage &msg) {
    if (msg.kind.isEmpty() || msg.action.isEmpty()) {
        return {};
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("ver"), msg.ver);
    obj.insert(QStringLiteral("kind"), msg.kind);
    obj.insert(QStringLiteral("action"), msg.action);
    obj.insert(QStringLiteral("req_id"), msg.reqId);
    obj.insert(QStringLiteral("ok"), msg.ok);
    obj.insert(QStringLiteral("payload"), msg.payload);
    return obj;
}

bool ipcMessageFromJson(const QJsonObject &obj, IpcMessage *out) {
    if (!out) {
        return false;
    }

    const QJsonValue kindVal = obj.value(QStringLiteral("kind"));
    const QJsonValue actionVal = obj.value(QStringLiteral("action"));
    if (!kindVal.isString() || kindVal.toString().isEmpty()) {
        return false;
    }
    if (!actionVal.isString() || actionVal.toString().isEmpty()) {
        return false;
    }

    IpcMessage msg;
    msg.kind = kindVal.toString();
    msg.action = actionVal.toString();

    const QJsonValue verVal = obj.value(QStringLiteral("ver"));
    if (verVal.isUndefined() || !verVal.isDouble()) {
        return false;
    }

    const double rawVer = verVal.toDouble();
    double integralVer = 0.0;
    if (!qIsFinite(rawVer)
        || std::modf(rawVer, &integralVer) != 0.0
        || rawVer < static_cast<double>(std::numeric_limits<int>::min())
        || rawVer > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    msg.ver = static_cast<int>(integralVer);

    const QJsonValue payloadVal = obj.value(QStringLiteral("payload"));
    if (payloadVal.isUndefined() || !payloadVal.isObject()) {
        return false;
    }

    const QJsonValue reqIdVal = obj.value(QStringLiteral("req_id"));
    if (!reqIdVal.isUndefined()) {
        if (!reqIdVal.isString()) {
            return false;
        }
        msg.reqId = reqIdVal.toString();
    }

    const QJsonValue okVal = obj.value(QStringLiteral("ok"));
    if (!okVal.isUndefined()) {
        if (!okVal.isBool()) {
            return false;
        }
        msg.ok = okVal.toBool();
    }

    msg.payload = payloadVal.toObject();

    *out = msg;
    return true;
}
