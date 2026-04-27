#include "pose/pose_stage_timing_logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

PoseStageTimingLogger::PoseStageTimingLogger(const QString &path)
    : path_(path) {
}

bool PoseStageTimingLogger::isEnabled() const {
    return !path_.isEmpty();
}

void PoseStageTimingLogger::appendSample(
    const AnalysisFramePacket &frame, const PoseStageTimingSample &sample) const {
    if (!isEnabled()) {
        return;
    }

    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QJsonObject json;
    json.insert(QStringLiteral("frame_id"), static_cast<qint64>(frame.frameId));
    json.insert(QStringLiteral("camera_id"), frame.cameraId);
    json.insert(QStringLiteral("pixel_format"), pixelFormatName(frame.pixelFormat));
    json.insert(QStringLiteral("frame_ts"), frame.timestampMs);
    json.insert(QStringLiteral("width"), frame.width);
    json.insert(QStringLiteral("height"), frame.height);
    json.insert(QStringLiteral("preprocess_ms"), sample.preprocessMs);
    json.insert(QStringLiteral("inputs_set_ms"), sample.inputsSetMs);
    json.insert(QStringLiteral("rknn_run_ms"), sample.rknnRunMs);
    json.insert(QStringLiteral("outputs_get_ms"), sample.outputsGetMs);
    json.insert(QStringLiteral("io_mem_path"), sample.ioMemPath);
    json.insert(QStringLiteral("output_prealloc_path"), sample.outputPreallocPath);
    json.insert(QStringLiteral("post_process_ms"), sample.postProcessMs);
    json.insert(QStringLiteral("total_ms"), sample.totalMs);
    json.insert(QStringLiteral("people_count"), sample.peopleCount);

    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QString PoseStageTimingLogger::pixelFormatName(AnalysisPixelFormat pixelFormat) const {
    switch (pixelFormat) {
    case AnalysisPixelFormat::Jpeg:
        return QStringLiteral("jpeg");
    case AnalysisPixelFormat::Nv12:
        return QStringLiteral("nv12");
    case AnalysisPixelFormat::Rgb:
        return QStringLiteral("rgb");
    }
    return QStringLiteral("unknown");
}
