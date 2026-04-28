#include "runtime_config/app_runtime_config_validator.h"

namespace {
bool isOneOf(const QString &value, std::initializer_list<const char *> allowed) {
    for (const char *candidate : allowed) {
        if (value == QLatin1String(candidate)) {
            return true;
        }
    }
    return false;
}

void addError(QStringList *errors, const QString &text) {
    if (errors) {
        errors->append(text);
    }
}

void addWarning(QStringList *warnings, const QString &text) {
    if (warnings) {
        warnings->append(text);
    }
}
}

void validateAppRuntimeConfig(
    const AppRuntimeConfig &config, QStringList *errors, QStringList *warnings) {
    if (!isOneOf(config.system.runtimeMode, {"auto", "system", "bundle"})) {
        addError(errors, QStringLiteral("system.runtime_mode must be auto, system, or bundle"));
    }
    if (!isOneOf(config.video.pipelineBackend, {"process", "inproc_gst"})) {
        addError(errors, QStringLiteral("video.pipeline_backend must be process or inproc_gst"));
    }
    if (!isOneOf(config.video.analysisConvertBackend, {"gstreamer_cpu", "rga"})) {
        addError(errors, QStringLiteral("video.analysis_convert_backend must be gstreamer_cpu or rga"));
    }
    if (!isOneOf(config.analysis.transport, {"shared_memory", "dmabuf", "dma"})) {
        addError(errors, QStringLiteral("analysis.transport must be shared_memory or dmabuf"));
    }
    if (!isOneOf(config.fallDetection.actionBackend, {"lstm_rknn", "rule_based", "stgcn_rknn"})) {
        addError(errors, QStringLiteral("fall_detection.action_backend must be lstm_rknn, rule_based, or stgcn_rknn"));
    }
    if (!isOneOf(config.fallDetection.rknnIoMemMode, {"default", "zero_copy", "full"})) {
        addError(errors, QStringLiteral("fall_detection.rknn_io_mem_mode must be default, zero_copy, or full"));
    }
    if (config.video.devicePath.trimmed().isEmpty()) {
        addError(errors, QStringLiteral("video.device_path is empty"));
    }
    if (config.fallDetection.sequenceLength <= 0) {
        addError(errors, QStringLiteral("fall_detection.sequence_length must be greater than 0"));
    }
    if (config.fallDetection.maxTracks <= 0) {
        addError(errors, QStringLiteral("fall_detection.max_tracks must be greater than 0"));
    }
    if (config.analysis.gstForceDmabufIo && !config.analysis.gstDmabufInput) {
        addWarning(warnings, QStringLiteral("analysis.gst_force_dmabuf_io is enabled without analysis.gst_dmabuf_input"));
    }
}
