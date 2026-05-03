#pragma once

#include "models/video_models.h"
#include "runtime_config/app_runtime_config.h"

#include <QString>

class GstCommandBuilder {
public:
    explicit GstCommandBuilder(const AppRuntimeConfig &runtimeConfig);

    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status, bool analysisTapEnabled) const;
    QString buildRecordingCommand(
        const VideoChannelStatus &status, const QString &outputPath, bool analysisTapEnabled) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildPreviewStreamRecordingCommand(
        const QString &previewUrl, const QString &outputPath, QString *error) const;

private:
    enum class AnalysisConvertBackend {
        GstreamerCpu,
        Rga,
    };

    QString gstLaunchBinary() const;
    QString shellQuote(const QString &value) const;
    QString buildAnalysisTapCommandFragment(
        const VideoChannelStatus &status, const VideoProfile &sourceProfile, bool enabled) const;
    AnalysisConvertBackend analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const;

    AppRuntimeConfig runtimeConfig_;
};
