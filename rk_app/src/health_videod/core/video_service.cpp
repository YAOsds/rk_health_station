#include "core/video_service.h"

#include "debug/latency_marker_writer.h"
#include "analysis/gstreamer_analysis_output_backend.h"
#include "pipeline/gstreamer_video_pipeline_backend.h"
#include "pipeline/video_pipeline_backend.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace {
const char kDefaultCameraId[] = "front_cam";
const char kDefaultDevicePath[] = "/dev/video11";
const char kDefaultStorageDir[] = "/home/elf/videosurv/";
const char kAnalysisEnabledEnvVar[] = "RK_VIDEO_ANALYSIS_ENABLED";
const char kVideoLatencyMarkerEnvVar[] = "RK_VIDEO_LATENCY_MARKER_PATH";
}

VideoService::VideoService(
    VideoPipelineBackend *pipelineBackend, AnalysisOutputBackend *analysisBackend, QObject *parent)
    : QObject(parent)
    , pipelineBackend_(pipelineBackend ? pipelineBackend : new GstreamerVideoPipelineBackend())
    , analysisBackend_(analysisBackend ? analysisBackend : new GstreamerAnalysisOutputBackend())
    , ownsPipelineBackend_(!pipelineBackend)
    , ownsAnalysisBackend_(!analysisBackend) {
    pipelineBackend_->setObserver(this);
    pipelineBackend_->setAnalysisFrameSource(analysisBackend_);
    initializeDefaultChannels();
}

VideoService::~VideoService() {
    if (ownsAnalysisBackend_) {
        delete analysisBackend_;
        analysisBackend_ = nullptr;
    }
    if (ownsPipelineBackend_) {
        delete pipelineBackend_;
        pipelineBackend_ = nullptr;
    }
}

VideoChannelStatus VideoService::statusForCamera(const QString &cameraId) const {
    return channels_.value(cameraId);
}

AnalysisChannelStatus VideoService::analysisStatusForCamera(const QString &cameraId) const {
    if (!analysisBackend_) {
        AnalysisChannelStatus status;
        status.cameraId = cameraId;
        return status;
    }
    return analysisBackend_->statusForCamera(cameraId);
}

VideoCommandResult VideoService::applyStorageDir(const QString &cameraId, const QString &storageDir) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("set_storage_dir"),
            QStringLiteral("camera_not_found"));
    }

    const QString normalized = storageService_.normalizeDir(storageDir);
    QString errorCode;
    if (!storageService_.ensureWritableDirectory(normalized, &errorCode)) {
        channels_[cameraId].lastError = errorCode;
        return buildErrorResult(cameraId, QStringLiteral("set_storage_dir"), errorCode);
    }

    VideoChannelStatus &channel = channels_[cameraId];
    channel.storageDir = normalized;
    channel.lastError.clear();

    QJsonObject payload;
    payload.insert(QStringLiteral("storage_dir"), normalized);
    return buildOkResult(cameraId, QStringLiteral("set_storage_dir"), payload);
}

VideoCommandResult VideoService::startPreview(const QString &cameraId) {
    QString errorCode;
    if (!ensurePreview(cameraId, &errorCode)) {
        return buildErrorResult(cameraId, QStringLiteral("start_preview"), errorCode);
    }
    return buildOkResult(cameraId, QStringLiteral("start_preview"),
        videoChannelStatusToJson(channels_.value(cameraId)));
}

VideoCommandResult VideoService::takeSnapshot(const QString &cameraId) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("take_snapshot"),
            QStringLiteral("camera_not_found"));
    }

    VideoChannelStatus &channel = channels_[cameraId];
    if (channel.inputMode == QStringLiteral("test_file")) {
        return buildErrorResult(cameraId, QStringLiteral("take_snapshot"),
            QStringLiteral("unsupported_in_test_mode"));
    }
    if (channel.recording) {
        return buildErrorResult(cameraId, QStringLiteral("take_snapshot"),
            QStringLiteral("busy_recording"));
    }

    QString storageError;
    if (!storageService_.ensureWritableDirectory(channel.storageDir, &storageError)) {
        channel.lastError = storageError;
        return buildErrorResult(
            cameraId, QStringLiteral("take_snapshot"), QStringLiteral("storage_dir_invalid"));
    }

    const QString outputPath = nextSnapshotPath(channel.storageDir);
    QString error;
    if (!pipelineBackend_->captureSnapshot(channel, outputPath, &error)) {
        channel.lastError = error;
        channel.cameraState = VideoCameraState::Error;
        return buildErrorResult(cameraId, QStringLiteral("take_snapshot"),
            QStringLiteral("snapshot_failed"));
    }

    channel.lastSnapshotPath = outputPath;
    channel.lastError.clear();

    QJsonObject payload;
    payload.insert(QStringLiteral("snapshot_path"), outputPath);
    return buildOkResult(cameraId, QStringLiteral("take_snapshot"), payload);
}

VideoCommandResult VideoService::startRecording(const QString &cameraId) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("start_recording"),
            QStringLiteral("camera_not_found"));
    }

    VideoChannelStatus &channel = channels_[cameraId];
    if (channel.inputMode == QStringLiteral("test_file")) {
        return buildErrorResult(cameraId, QStringLiteral("start_recording"),
            QStringLiteral("unsupported_in_test_mode"));
    }
    if (channel.recording) {
        return buildErrorResult(cameraId, QStringLiteral("start_recording"),
            QStringLiteral("already_recording"));
    }

    QString storageError;
    if (!storageService_.ensureWritableDirectory(channel.storageDir, &storageError)) {
        channel.lastError = storageError;
        return buildErrorResult(
            cameraId, QStringLiteral("start_recording"), QStringLiteral("storage_dir_invalid"));
    }

    QString previewError;
    if (!ensurePreview(cameraId, &previewError)) {
        return buildErrorResult(cameraId, QStringLiteral("start_recording"), previewError);
    }

    const QString outputPath = nextRecordPath(channel.storageDir);
    QString error;
    if (!pipelineBackend_->startRecording(channel, outputPath, &error)) {
        channel.lastError = error;
        channel.cameraState = VideoCameraState::Error;
        return buildErrorResult(cameraId, QStringLiteral("start_recording"),
            QStringLiteral("record_start_failed"));
    }

    channel.recording = true;
    channel.currentRecordPath = outputPath;
    channel.cameraState = VideoCameraState::Recording;
    channel.lastError.clear();

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=recording_started path=%2")
               .arg(cameraId)
               .arg(outputPath);

    QJsonObject payload;
    payload.insert(QStringLiteral("record_path"), outputPath);
    return buildOkResult(cameraId, QStringLiteral("start_recording"), payload);
}

VideoCommandResult VideoService::stopRecording(const QString &cameraId) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("stop_recording"),
            QStringLiteral("camera_not_found"));
    }

    VideoChannelStatus &channel = channels_[cameraId];
    if (!channel.recording) {
        return buildErrorResult(cameraId, QStringLiteral("stop_recording"),
            QStringLiteral("not_recording"));
    }

    QString error;
    if (!pipelineBackend_->stopRecording(cameraId, &error)) {
        channel.lastError = error;
        channel.cameraState = VideoCameraState::Error;
        return buildErrorResult(cameraId, QStringLiteral("stop_recording"),
            QStringLiteral("record_stop_failed"));
    }

    channel.recording = false;
    channel.lastError.clear();
    channel.cameraState = channel.previewUrl.isEmpty()
        ? VideoCameraState::Idle
        : VideoCameraState::Previewing;

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=recording_stopped path=%2")
               .arg(cameraId)
               .arg(channel.currentRecordPath);

    QJsonObject payload;
    payload.insert(QStringLiteral("record_path"), channel.currentRecordPath);
    return buildOkResult(cameraId, QStringLiteral("stop_recording"), payload);
}

VideoCommandResult VideoService::startTestInput(const QString &cameraId, const QString &filePath) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"),
            QStringLiteral("camera_not_found"));
    }

    QString validationError;
    if (!validateTestFilePath(filePath, &validationError)) {
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"), validationError);
    }

    VideoChannelStatus previous = channels_.value(cameraId);
    VideoChannelStatus &channel = channels_[cameraId];
    channel.recording = false;
    channel.currentRecordPath.clear();
    channel.inputMode = QStringLiteral("test_file");
    channel.testFilePath = QFileInfo(filePath).absoluteFilePath();
    channel.testPlaybackState = QStringLiteral("playing");
    channel.lastError.clear();

    QString errorCode;
    if (!restartPreviewForChannel(cameraId, &errorCode)) {
        channels_[cameraId] = previous;
        Q_UNUSED(errorCode);
        qWarning().noquote()
            << QStringLiteral("video_runtime camera=%1 event=start_test_input_failed error=%2")
                   .arg(cameraId)
                   .arg(QStringLiteral("test_input_start_failed"));
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"),
            QStringLiteral("test_input_start_failed"));
    }

    LatencyMarkerWriter marker(qEnvironmentVariable(kVideoLatencyMarkerEnvVar));
    marker.writeEvent(QStringLiteral("playback_started"), QDateTime::currentMSecsSinceEpoch(),
        QJsonObject{
            {QStringLiteral("camera_id"), cameraId},
            {QStringLiteral("file_path"), channel.testFilePath},
            {QStringLiteral("input_mode"), channel.inputMode},
        });

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=test_input_started file=%2")
               .arg(cameraId)
               .arg(channel.testFilePath);

    return buildOkResult(cameraId, QStringLiteral("start_test_input"),
        videoChannelStatusToJson(channels_.value(cameraId)));
}

VideoCommandResult VideoService::stopTestInput(const QString &cameraId) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("stop_test_input"),
            QStringLiteral("camera_not_found"));
    }

    VideoChannelStatus previous = channels_.value(cameraId);
    VideoChannelStatus &channel = channels_[cameraId];
    resetTestModeState(&channel);
    channel.lastError.clear();

    QString errorCode;
    if (!restartPreviewForChannel(cameraId, &errorCode)) {
        channels_[cameraId] = previous;
        Q_UNUSED(errorCode);
        qWarning().noquote()
            << QStringLiteral("video_runtime camera=%1 event=stop_test_input_failed error=%2")
                   .arg(cameraId)
                   .arg(QStringLiteral("test_input_stop_failed"));
        return buildErrorResult(cameraId, QStringLiteral("stop_test_input"),
            QStringLiteral("test_input_stop_failed"));
    }

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=test_input_stopped")
               .arg(cameraId);

    return buildOkResult(cameraId, QStringLiteral("stop_test_input"),
        videoChannelStatusToJson(channels_.value(cameraId)));
}

void VideoService::initializeDefaultChannels() {
    VideoChannelStatus channel;
    channel.cameraId = QString::fromUtf8(kDefaultCameraId);
    channel.displayName = QStringLiteral("Front Camera");
    channel.devicePath = QString::fromUtf8(kDefaultDevicePath);
    channel.cameraState = VideoCameraState::Idle;
    channel.storageDir = QString::fromUtf8(kDefaultStorageDir);
    channel.recording = false;
    channel.previewProfile.width = 640;
    channel.previewProfile.height = 480;
    channel.previewProfile.fps = 30;
    channel.previewProfile.pixelFormat = QStringLiteral("NV12");
    channel.snapshotProfile.width = 1920;
    channel.snapshotProfile.height = 1080;
    channel.snapshotProfile.pixelFormat = QStringLiteral("NV12");
    channel.recordProfile.width = 1280;
    channel.recordProfile.height = 720;
    channel.recordProfile.fps = 30;
    channel.recordProfile.pixelFormat = QStringLiteral("NV12");
    channel.recordProfile.codec = QStringLiteral("H264");
    channel.recordProfile.container = QStringLiteral("MP4");
    channels_.insert(channel.cameraId, channel);
}

bool VideoService::ensurePreview(const QString &cameraId, QString *errorCode) {
    if (errorCode) {
        errorCode->clear();
    }

    if (!channels_.contains(cameraId)) {
        if (errorCode) {
            *errorCode = QStringLiteral("camera_not_found");
        }
        return false;
    }

    VideoChannelStatus &channel = channels_[cameraId];
    if (!channel.previewUrl.isEmpty() && channel.cameraState == VideoCameraState::Previewing
        && !channel.recording) {
        return true;
    }

    if (!syncAnalysisOutput(cameraId, errorCode)) {
        channel.cameraState = VideoCameraState::Error;
        channel.lastError = errorCode ? *errorCode : QStringLiteral("analysis_unavailable");
        return false;
    }

    QString previewUrl;
    QString error;
    if (!pipelineBackend_->startPreview(channel, &previewUrl, &error)) {
        QString analysisError;
        analysisBackend_->stop(cameraId, &analysisError);
        channel.cameraState = VideoCameraState::Error;
        channel.lastError = error;
        if (errorCode) {
            *errorCode = QStringLiteral("preview_unavailable");
        }
        return false;
    }

    channel.previewUrl = previewUrl;
    channel.cameraState = channel.recording ? VideoCameraState::Recording : VideoCameraState::Previewing;
    channel.lastError.clear();
    return true;
}

bool VideoService::restartPreviewForChannel(const QString &cameraId, QString *errorCode) {
    if (errorCode) {
        errorCode->clear();
    }
    if (!channels_.contains(cameraId)) {
        if (errorCode) {
            *errorCode = QStringLiteral("camera_not_found");
        }
        return false;
    }

    VideoChannelStatus &channel = channels_[cameraId];
    QString stopError;
    if (!pipelineBackend_->stopPreview(cameraId, &stopError)) {
        channel.lastError = stopError;
        channel.cameraState = VideoCameraState::Error;
        if (errorCode) {
            *errorCode = QStringLiteral("preview_stop_failed");
        }
        return false;
    }

    channel.previewUrl.clear();
    channel.cameraState = VideoCameraState::Idle;
    return ensurePreview(cameraId, errorCode);
}

bool VideoService::validateTestFilePath(const QString &filePath, QString *errorCode) const {
    if (errorCode) {
        errorCode->clear();
    }
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        if (errorCode) {
            *errorCode = QStringLiteral("test_file_not_found");
        }
        return false;
    }
    if (!info.isReadable()) {
        if (errorCode) {
            *errorCode = QStringLiteral("test_file_invalid");
        }
        return false;
    }
    return true;
}

void VideoService::resetTestModeState(VideoChannelStatus *channel) const {
    if (!channel) {
        return;
    }
    channel->inputMode = QStringLiteral("camera");
    channel->testFilePath.clear();
    channel->testPlaybackState = QStringLiteral("idle");
}

bool VideoService::analysisEnabledForCamera(const QString &cameraId) const {
    Q_UNUSED(cameraId);
    return qEnvironmentVariableIntValue(kAnalysisEnabledEnvVar) == 1;
}

bool VideoService::syncAnalysisOutput(const QString &cameraId, QString *errorCode) {
    if (errorCode) {
        errorCode->clear();
    }

    QString error;
    if (!analysisBackend_) {
        return true;
    }

    if (cameraId.isEmpty() || !channels_.contains(cameraId)) {
        return true;
    }

    if (analysisEnabledForCamera(cameraId)) {
        if (!analysisBackend_->start(channels_.value(cameraId), &error)) {
            if (errorCode) {
                *errorCode = QStringLiteral("analysis_unavailable");
            }
            return false;
        }
        return true;
    }

    analysisBackend_->stop(cameraId, &error);
    return true;
}

void VideoService::onPipelinePlaybackFinished(const QString &cameraId) {
    if (!channels_.contains(cameraId)) {
        return;
    }
    VideoChannelStatus &channel = channels_[cameraId];
    if (channel.inputMode != QStringLiteral("test_file")) {
        return;
    }
    channel.testPlaybackState = QStringLiteral("finished");
    channel.lastError.clear();
}

void VideoService::onPipelineRuntimeError(const QString &cameraId, const QString &error) {
    if (!channels_.contains(cameraId)) {
        return;
    }
    VideoChannelStatus &channel = channels_[cameraId];
    channel.lastError = error;
    if (channel.inputMode == QStringLiteral("test_file")) {
        channel.testPlaybackState = QStringLiteral("error");
    }
}

QString VideoService::nextSnapshotPath(const QString &storageDir) const {
    return QDir(storageDir).filePath(
        QStringLiteral("snapshot_%1.jpg").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
}

QString VideoService::nextRecordPath(const QString &storageDir) const {
    return QDir(storageDir).filePath(
        QStringLiteral("record_%1.mp4").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
}

VideoCommandResult VideoService::buildErrorResult(
    const QString &cameraId, const QString &action, const QString &errorCode) const {
    VideoCommandResult result;
    result.action = action;
    result.requestId = QStringLiteral("server");
    result.cameraId = cameraId;
    result.ok = false;
    result.errorCode = errorCode;
    return result;
}

VideoCommandResult VideoService::buildOkResult(
    const QString &cameraId, const QString &action, const QJsonObject &payload) const {
    VideoCommandResult result;
    result.action = action;
    result.requestId = QStringLiteral("server");
    result.cameraId = cameraId;
    result.ok = true;
    result.payload = payload;
    return result;
}
