#pragma once

#include <QString>

#include <optional>

struct VideoRuntimeLogSummary {
    QString cameraId;
    QString inputMode;
    bool consumerConnected = false;
    int publishedFramesWindow = 0;
    double publishFps = 0.0;
    quint64 droppedFramesTotal = 0;
    quint64 droppedFramesDelta = 0;
};

class VideoRuntimeLogStats {
public:
    explicit VideoRuntimeLogStats(qint64 intervalMs = 5000);

    void onDescriptorPublished(const QString &cameraId, const QString &inputMode,
        bool consumerConnected, quint64 droppedFramesTotal, qint64 nowMs);
    std::optional<VideoRuntimeLogSummary> takeSummaryIfDue(qint64 nowMs);

private:
    qint64 intervalMs_ = 5000;
    qint64 windowStartMs_ = -1;
    QString cameraId_;
    QString inputMode_;
    bool consumerConnected_ = false;
    int publishedFramesWindow_ = 0;
    quint64 droppedFramesTotal_ = 0;
    quint64 droppedFramesWindowStart_ = 0;
};
