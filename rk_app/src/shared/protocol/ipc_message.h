#pragma once

#include <QJsonObject>
#include <QString>

struct IpcMessage {
    int ver = 1;
    QString kind;
    QString action;
    QString reqId;
    bool ok = true;
    QJsonObject payload;
};

// Returns an empty object when required outbound fields are invalid.
QJsonObject ipcMessageToJson(const IpcMessage &msg);
bool ipcMessageFromJson(const QJsonObject &obj, IpcMessage *out);
