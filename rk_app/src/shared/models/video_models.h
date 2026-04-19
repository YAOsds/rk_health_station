#pragma once

#include <QString>

struct VideoProfile {
    int width = 0;
    int height = 0;
    int fps = 0;
    QString pixelFormat;
    QString codec;
    QString container;
};

enum class VideoCameraState {
    Unavailable,
    Idle,
    Previewing,
    Recording,
    Error,
};

struct VideoChannelStatus {
    QString cameraId;
    QString displayName;
    QString devicePath;
    VideoCameraState cameraState = VideoCameraState::Unavailable;
    QString previewUrl;
    QString storageDir;
    QString lastSnapshotPath;
    QString currentRecordPath;
    QString lastError;
    bool recording = false;
    QString inputMode = QStringLiteral("camera");
    QString testFilePath;
    QString testPlaybackState = QStringLiteral("idle");
    VideoProfile previewProfile;
    VideoProfile snapshotProfile;
    VideoProfile recordProfile;
};
