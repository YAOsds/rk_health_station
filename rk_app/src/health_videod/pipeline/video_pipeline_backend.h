#pragma once

#include "analysis/analysis_frame_source.h"
#include "models/video_models.h"

#include <QString>

class VideoPipelineObserver {
public:
    virtual ~VideoPipelineObserver() = default;

    virtual void onPipelinePlaybackFinished(const QString &cameraId) = 0;
    virtual void onPipelineRuntimeError(const QString &cameraId, const QString &error) = 0;
};

class VideoPipelineBackend {
public:
    virtual ~VideoPipelineBackend() = default;

    virtual void setObserver(VideoPipelineObserver *observer) = 0;
    virtual void setAnalysisFrameSource(AnalysisFrameSource *source) = 0;
    virtual bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) = 0;
    virtual bool stopPreview(const QString &cameraId, QString *error) = 0;
    virtual bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool stopRecording(const QString &cameraId, QString *error) = 0;
};
