#pragma once

#include "core/video_storage_service.h"
#include "models/fall_models.h"
#include "pipeline/video_pipeline_backend.h"
#include "protocol/video_ipc.h"

#include <QHash>
#include <QObject>

class AnalysisOutputBackend;

class VideoService : public QObject, private VideoPipelineObserver {
    Q_OBJECT

public:
    explicit VideoService(VideoPipelineBackend *pipelineBackend = nullptr,
        AnalysisOutputBackend *analysisBackend = nullptr, QObject *parent = nullptr);
    ~VideoService() override;

    VideoChannelStatus statusForCamera(const QString &cameraId) const;
    AnalysisChannelStatus analysisStatusForCamera(const QString &cameraId) const;
    VideoCommandResult applyStorageDir(const QString &cameraId, const QString &storageDir);
    VideoCommandResult startPreview(const QString &cameraId);
    VideoCommandResult takeSnapshot(const QString &cameraId);
    VideoCommandResult startRecording(const QString &cameraId);
    VideoCommandResult stopRecording(const QString &cameraId);
    VideoCommandResult startTestInput(const QString &cameraId, const QString &filePath);
    VideoCommandResult stopTestInput(const QString &cameraId);

private:
    void initializeDefaultChannels();
    bool ensurePreview(const QString &cameraId, QString *errorCode);
    bool restartPreviewForChannel(const QString &cameraId, QString *errorCode);
    bool validateTestFilePath(const QString &filePath, QString *errorCode) const;
    void resetTestModeState(VideoChannelStatus *channel) const;
    bool analysisEnabledForCamera(const QString &cameraId) const;
    void syncAnalysisOutput(const QString &cameraId);
    QString nextSnapshotPath(const QString &storageDir) const;
    QString nextRecordPath(const QString &storageDir) const;
    void onPipelinePlaybackFinished(const QString &cameraId) override;
    void onPipelineRuntimeError(const QString &cameraId, const QString &error) override;
    VideoCommandResult buildErrorResult(
        const QString &cameraId, const QString &action, const QString &errorCode) const;
    VideoCommandResult buildOkResult(const QString &cameraId, const QString &action,
        const QJsonObject &payload = QJsonObject()) const;

    QHash<QString, VideoChannelStatus> channels_;
    VideoStorageService storageService_;
    VideoPipelineBackend *pipelineBackend_ = nullptr;
    AnalysisOutputBackend *analysisBackend_ = nullptr;
    bool ownsPipelineBackend_ = false;
    bool ownsAnalysisBackend_ = false;
};
