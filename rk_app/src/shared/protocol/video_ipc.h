#pragma once

#include "models/video_models.h"

#include <QJsonObject>
#include <QString>

struct VideoCommand {
    QString action;
    QString requestId;
    QString cameraId;
    QJsonObject payload;
};

struct VideoCommandResult {
    QString action;
    QString requestId;
    QString cameraId;
    bool ok = false;
    QString errorCode;
    QJsonObject payload;
};

QString videoCameraStateToString(VideoCameraState state);
bool videoCameraStateFromString(const QString &value, VideoCameraState *state);
QJsonObject videoProfileToJson(const VideoProfile &profile);
bool videoProfileFromJson(const QJsonObject &json, VideoProfile *profile);
QJsonObject videoChannelStatusToJson(const VideoChannelStatus &status);
bool videoChannelStatusFromJson(const QJsonObject &json, VideoChannelStatus *status);
QJsonObject videoCommandToJson(const VideoCommand &command);
bool videoCommandFromJson(const QJsonObject &json, VideoCommand *command);
QJsonObject videoCommandResultToJson(const VideoCommandResult &result);
bool videoCommandResultFromJson(const QJsonObject &json, VideoCommandResult *result);
