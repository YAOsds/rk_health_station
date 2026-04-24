#include "debug/fall_runtime_log_stats.h"

#include <QtGlobal>

FallRuntimeLogStats::FallRuntimeLogStats(qint64 intervalMs)
    : intervalMs_(intervalMs) {
}

void FallRuntimeLogStats::onFrameIngested(qint64 nowMs) {
    if (windowStartMs_ < 0) {
        windowStartMs_ = nowMs;
    }
    ingestedFramesWindow_ += 1;
}

void FallRuntimeLogStats::onInferenceComplete(
    qint64 nowMs, bool hasPeople, bool nonEmptyBatch, double inferMs) {
    if (windowStartMs_ < 0) {
        windowStartMs_ = nowMs;
    }
    inferredFramesWindow_ += 1;
    peopleFramesWindow_ += hasPeople ? 1 : 0;
    emptyFramesWindow_ += hasPeople ? 0 : 1;
    nonEmptyBatchWindow_ += nonEmptyBatch ? 1 : 0;
    inferMsTotalWindow_ += inferMs;
}

std::optional<FallRuntimeLogSummary> FallRuntimeLogStats::takeSummaryIfDue(const QString &cameraId,
    const QString &latestState, double latestConfidence, const QString &latestError,
    qint64 nowMs) {
    if (windowStartMs_ < 0 || inferredFramesWindow_ == 0 || (nowMs - windowStartMs_) < intervalMs_) {
        return std::nullopt;
    }

    const qint64 elapsedMs = qMax<qint64>(1, nowMs - windowStartMs_);

    FallRuntimeLogSummary summary;
    summary.cameraId = cameraId;
    summary.ingestFps = (ingestedFramesWindow_ * 1000.0) / elapsedMs;
    summary.inferFps = (inferredFramesWindow_ * 1000.0) / elapsedMs;
    summary.avgInferMs = inferMsTotalWindow_ / inferredFramesWindow_;
    summary.peopleFrames = peopleFramesWindow_;
    summary.emptyFrames = emptyFramesWindow_;
    summary.nonEmptyBatchCount = nonEmptyBatchWindow_;
    summary.latestState = latestState;
    summary.latestConfidence = latestConfidence;
    summary.latestError = latestError;

    windowStartMs_ = nowMs;
    ingestedFramesWindow_ = 0;
    inferredFramesWindow_ = 0;
    peopleFramesWindow_ = 0;
    emptyFramesWindow_ = 0;
    nonEmptyBatchWindow_ = 0;
    inferMsTotalWindow_ = 0.0;
    return summary;
}
