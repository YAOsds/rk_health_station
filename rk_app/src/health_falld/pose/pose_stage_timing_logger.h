#pragma once

#include "protocol/analysis_stream_protocol.h"

#include <QString>

struct PoseStageTimingSample {
    qint64 preprocessMs = 0;
    qint64 inputsSetMs = 0;
    qint64 rknnRunMs = 0;
    qint64 outputsGetMs = 0;
    bool ioMemPath = false;
    bool outputPreallocPath = false;
    qint64 postProcessMs = 0;
    qint64 totalMs = 0;
    int peopleCount = 0;
};

class PoseStageTimingLogger {
public:
    explicit PoseStageTimingLogger(const QString &path);

    bool isEnabled() const;
    void appendSample(const AnalysisFramePacket &frame, const PoseStageTimingSample &sample) const;

private:
    QString pixelFormatName(AnalysisPixelFormat pixelFormat) const;

    QString path_;
};
