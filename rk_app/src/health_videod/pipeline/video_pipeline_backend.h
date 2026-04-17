#pragma once

#include "models/video_models.h"

#include <QString>

class VideoPipelineBackend {
public:
    virtual ~VideoPipelineBackend() = default;

    virtual bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) = 0;
    virtual bool stopPreview(const QString &cameraId, QString *error) = 0;
    virtual bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool stopRecording(const QString &cameraId, QString *error) = 0;
};
