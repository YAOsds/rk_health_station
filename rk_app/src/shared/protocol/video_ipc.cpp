#include "protocol/video_ipc.h"

namespace {
QJsonObject buildProfileJson(const VideoProfile &profile) {
    QJsonObject json;
    json.insert(QStringLiteral("width"), profile.width);
    json.insert(QStringLiteral("height"), profile.height);
    json.insert(QStringLiteral("fps"), profile.fps);
    json.insert(QStringLiteral("pixel_format"), profile.pixelFormat);
    json.insert(QStringLiteral("codec"), profile.codec);
    json.insert(QStringLiteral("container"), profile.container);
    return json;
}

bool parseProfileJson(const QJsonObject &json, VideoProfile *profile) {
    if (!profile) {
        return false;
    }

    VideoProfile parsed;
    parsed.width = json.value(QStringLiteral("width")).toInt();
    parsed.height = json.value(QStringLiteral("height")).toInt();
    parsed.fps = json.value(QStringLiteral("fps")).toInt();
    parsed.pixelFormat = json.value(QStringLiteral("pixel_format")).toString();
    parsed.codec = json.value(QStringLiteral("codec")).toString();
    parsed.container = json.value(QStringLiteral("container")).toString();
    *profile = parsed;
    return true;
}
}

QString videoCameraStateToString(VideoCameraState state) {
    switch (state) {
    case VideoCameraState::Unavailable:
        return QStringLiteral("unavailable");
    case VideoCameraState::Idle:
        return QStringLiteral("idle");
    case VideoCameraState::Previewing:
        return QStringLiteral("previewing");
    case VideoCameraState::Recording:
        return QStringLiteral("recording");
    case VideoCameraState::Error:
        return QStringLiteral("error");
    }

    return QStringLiteral("unavailable");
}

bool videoCameraStateFromString(const QString &value, VideoCameraState *state) {
    if (!state) {
        return false;
    }

    if (value == QStringLiteral("unavailable")) {
        *state = VideoCameraState::Unavailable;
        return true;
    }
    if (value == QStringLiteral("idle")) {
        *state = VideoCameraState::Idle;
        return true;
    }
    if (value == QStringLiteral("previewing")) {
        *state = VideoCameraState::Previewing;
        return true;
    }
    if (value == QStringLiteral("recording")) {
        *state = VideoCameraState::Recording;
        return true;
    }
    if (value == QStringLiteral("error")) {
        *state = VideoCameraState::Error;
        return true;
    }
    return false;
}

QJsonObject videoProfileToJson(const VideoProfile &profile) {
    return buildProfileJson(profile);
}

bool videoProfileFromJson(const QJsonObject &json, VideoProfile *profile) {
    return parseProfileJson(json, profile);
}

QJsonObject videoChannelStatusToJson(const VideoChannelStatus &status) {
    QJsonObject json;
    json.insert(QStringLiteral("camera_id"), status.cameraId);
    json.insert(QStringLiteral("display_name"), status.displayName);
    json.insert(QStringLiteral("device_path"), status.devicePath);
    json.insert(QStringLiteral("camera_state"), videoCameraStateToString(status.cameraState));
    json.insert(QStringLiteral("preview_url"), status.previewUrl);
    json.insert(QStringLiteral("storage_dir"), status.storageDir);
    json.insert(QStringLiteral("last_snapshot_path"), status.lastSnapshotPath);
    json.insert(QStringLiteral("current_record_path"), status.currentRecordPath);
    json.insert(QStringLiteral("last_error"), status.lastError);
    json.insert(QStringLiteral("recording"), status.recording);
    json.insert(QStringLiteral("input_mode"), status.inputMode);
    json.insert(QStringLiteral("test_file_path"), status.testFilePath);
    json.insert(QStringLiteral("test_playback_state"), status.testPlaybackState);
    json.insert(QStringLiteral("preview_profile"), buildProfileJson(status.previewProfile));
    json.insert(QStringLiteral("snapshot_profile"), buildProfileJson(status.snapshotProfile));
    json.insert(QStringLiteral("record_profile"), buildProfileJson(status.recordProfile));
    return json;
}

bool videoChannelStatusFromJson(const QJsonObject &json, VideoChannelStatus *status) {
    if (!status) {
        return false;
    }

    const QString stateString = json.value(QStringLiteral("camera_state")).toString();
    VideoCameraState state = VideoCameraState::Unavailable;
    if (stateString.isEmpty() || !videoCameraStateFromString(stateString, &state)) {
        return false;
    }

    VideoChannelStatus parsed;
    parsed.cameraId = json.value(QStringLiteral("camera_id")).toString();
    parsed.displayName = json.value(QStringLiteral("display_name")).toString();
    parsed.devicePath = json.value(QStringLiteral("device_path")).toString();
    parsed.cameraState = state;
    parsed.previewUrl = json.value(QStringLiteral("preview_url")).toString();
    parsed.storageDir = json.value(QStringLiteral("storage_dir")).toString();
    parsed.lastSnapshotPath = json.value(QStringLiteral("last_snapshot_path")).toString();
    parsed.currentRecordPath = json.value(QStringLiteral("current_record_path")).toString();
    parsed.lastError = json.value(QStringLiteral("last_error")).toString();
    parsed.recording = json.value(QStringLiteral("recording")).toBool();
    parsed.inputMode = json.value(QStringLiteral("input_mode")).toString(QStringLiteral("camera"));
    parsed.testFilePath = json.value(QStringLiteral("test_file_path")).toString();
    parsed.testPlaybackState = json.value(QStringLiteral("test_playback_state"))
                                   .toString(QStringLiteral("idle"));
    parseProfileJson(json.value(QStringLiteral("preview_profile")).toObject(), &parsed.previewProfile);
    parseProfileJson(json.value(QStringLiteral("snapshot_profile")).toObject(), &parsed.snapshotProfile);
    parseProfileJson(json.value(QStringLiteral("record_profile")).toObject(), &parsed.recordProfile);
    *status = parsed;
    return true;
}

QJsonObject videoCommandToJson(const VideoCommand &command) {
    QJsonObject json;
    json.insert(QStringLiteral("action"), command.action);
    json.insert(QStringLiteral("request_id"), command.requestId);
    json.insert(QStringLiteral("camera_id"), command.cameraId);
    json.insert(QStringLiteral("payload"), command.payload);
    return json;
}

bool videoCommandFromJson(const QJsonObject &json, VideoCommand *command) {
    if (!command) {
        return false;
    }

    const QString action = json.value(QStringLiteral("action")).toString();
    const QString requestId = json.value(QStringLiteral("request_id")).toString();
    const QString cameraId = json.value(QStringLiteral("camera_id")).toString();
    if (action.isEmpty() || requestId.isEmpty() || cameraId.isEmpty()) {
        return false;
    }

    VideoCommand parsed;
    parsed.action = action;
    parsed.requestId = requestId;
    parsed.cameraId = cameraId;
    parsed.payload = json.value(QStringLiteral("payload")).toObject();
    *command = parsed;
    return true;
}

QJsonObject videoCommandResultToJson(const VideoCommandResult &result) {
    QJsonObject json;
    json.insert(QStringLiteral("action"), result.action);
    json.insert(QStringLiteral("request_id"), result.requestId);
    json.insert(QStringLiteral("camera_id"), result.cameraId);
    json.insert(QStringLiteral("ok"), result.ok);
    json.insert(QStringLiteral("error_code"), result.errorCode);
    json.insert(QStringLiteral("payload"), result.payload);
    return json;
}

bool videoCommandResultFromJson(const QJsonObject &json, VideoCommandResult *result) {
    if (!result) {
        return false;
    }

    const QString action = json.value(QStringLiteral("action")).toString();
    const QString requestId = json.value(QStringLiteral("request_id")).toString();
    const QString cameraId = json.value(QStringLiteral("camera_id")).toString();
    if (action.isEmpty() || requestId.isEmpty() || cameraId.isEmpty()) {
        return false;
    }

    VideoCommandResult parsed;
    parsed.action = action;
    parsed.requestId = requestId;
    parsed.cameraId = cameraId;
    parsed.ok = json.value(QStringLiteral("ok")).toBool();
    parsed.errorCode = json.value(QStringLiteral("error_code")).toString();
    parsed.payload = json.value(QStringLiteral("payload")).toObject();
    *result = parsed;
    return true;
}
