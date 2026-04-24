#include "debug/video_runtime_log_stats.h"

#include <QtGlobal>

VideoRuntimeLogStats::VideoRuntimeLogStats(qint64 intervalMs)
    : intervalMs_(intervalMs) {
}

void VideoRuntimeLogStats::onDescriptorPublished(const QString &cameraId, const QString &inputMode,
    bool consumerConnected, quint64 droppedFramesTotal, qint64 nowMs) {
    if (windowStartMs_ < 0) {
        windowStartMs_ = nowMs;
    }

    cameraId_ = cameraId;
    inputMode_ = inputMode;
    consumerConnected_ = consumerConnected;
    droppedFramesTotal_ = droppedFramesTotal;
    publishedFramesWindow_ += 1;
}

std::optional<VideoRuntimeLogSummary> VideoRuntimeLogStats::takeSummaryIfDue(qint64 nowMs) {
    if (windowStartMs_ < 0 || publishedFramesWindow_ == 0 || (nowMs - windowStartMs_) < intervalMs_) {
        return std::nullopt;
    }

    const qint64 elapsedMs = qMax<qint64>(1, nowMs - windowStartMs_);

    VideoRuntimeLogSummary summary;
    summary.cameraId = cameraId_;
    summary.inputMode = inputMode_;
    summary.consumerConnected = consumerConnected_;
    summary.publishedFramesWindow = publishedFramesWindow_;
    summary.publishFps = (publishedFramesWindow_ * 1000.0) / elapsedMs;
    summary.droppedFramesTotal = droppedFramesTotal_;
    summary.droppedFramesDelta = droppedFramesTotal_ - droppedFramesWindowStart_;

    windowStartMs_ = nowMs;
    publishedFramesWindow_ = 0;
    droppedFramesWindowStart_ = droppedFramesTotal_;
    return summary;
}
