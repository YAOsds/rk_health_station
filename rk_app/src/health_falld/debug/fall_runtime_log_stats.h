#pragma once

#include <QString>

#include <optional>

struct FallRuntimeLogSummary {
    QString cameraId;
    double ingestFps = 0.0;
    double inferFps = 0.0;
    double avgInferMs = 0.0;
    int peopleFrames = 0;
    int emptyFrames = 0;
    int nonEmptyBatchCount = 0;
    QString latestState;
    double latestConfidence = 0.0;
    QString latestError;
};

class FallRuntimeLogStats {
public:
    explicit FallRuntimeLogStats(qint64 intervalMs = 5000);

    void onFrameIngested(qint64 nowMs);
    void onInferenceComplete(qint64 nowMs, bool hasPeople, bool nonEmptyBatch, double inferMs);
    std::optional<FallRuntimeLogSummary> takeSummaryIfDue(const QString &cameraId,
        const QString &latestState, double latestConfidence, const QString &latestError,
        qint64 nowMs);

private:
    qint64 intervalMs_ = 5000;
    qint64 windowStartMs_ = -1;
    int ingestedFramesWindow_ = 0;
    int inferredFramesWindow_ = 0;
    int peopleFramesWindow_ = 0;
    int emptyFramesWindow_ = 0;
    int nonEmptyBatchWindow_ = 0;
    double inferMsTotalWindow_ = 0.0;
};
