#include "runtime_config/app_runtime_config_loader.h"

#include "runtime_config/app_runtime_config_paths.h"
#include "runtime_config/app_runtime_config_validator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
void markOrigin(QHash<QString, QString> *origins, const QString &key, const QString &value) {
    if (origins) {
        origins->insert(key, value);
    }
}

QString readString(const QJsonObject &object, const QString &key, const QString &current,
    QHash<QString, QString> *origins, const QString &originKey) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return current;
    }
    markOrigin(origins, originKey, QStringLiteral("config"));
    return value.toString();
}

bool readBool(const QJsonObject &object, const QString &key, bool current,
    QHash<QString, QString> *origins, const QString &originKey) {
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        return current;
    }
    markOrigin(origins, originKey, QStringLiteral("config"));
    return value.toBool();
}

int readInt(const QJsonObject &object, const QString &key, int current,
    QHash<QString, QString> *origins, const QString &originKey) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return current;
    }
    markOrigin(origins, originKey, QStringLiteral("config"));
    return value.toInt();
}

double readDouble(const QJsonObject &object, const QString &key, double current,
    QHash<QString, QString> *origins, const QString &originKey) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return current;
    }
    markOrigin(origins, originKey, QStringLiteral("config"));
    return value.toDouble();
}

bool envBool(const char *name, bool current, bool *applied) {
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value.isEmpty()) {
        return current;
    }
    if (applied) {
        *applied = true;
    }
    return value == QStringLiteral("1") || value == QStringLiteral("true")
        || value == QStringLiteral("yes") || value == QStringLiteral("on");
}

void applyJson(const QJsonObject &root, AppRuntimeConfig *config, QHash<QString, QString> *origins) {
    if (!config) {
        return;
    }

    const QJsonObject system = root.value(QStringLiteral("system")).toObject();
    config->system.runtimeMode = readString(system, QStringLiteral("runtime_mode"),
        config->system.runtimeMode, origins, QStringLiteral("system.runtime_mode"));

    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    config->paths.storageDir = readString(paths, QStringLiteral("storage_dir"),
        config->paths.storageDir, origins, QStringLiteral("paths.storage_dir"));
    config->paths.databasePath = readString(paths, QStringLiteral("database_path"),
        config->paths.databasePath, origins, QStringLiteral("paths.database_path"));

    const QJsonObject ipc = root.value(QStringLiteral("ipc")).toObject();
    config->ipc.healthSocketPath = readString(ipc, QStringLiteral("health_socket"),
        config->ipc.healthSocketPath, origins, QStringLiteral("ipc.health_socket"));
    config->ipc.videoSocketPath = readString(ipc, QStringLiteral("video_socket"),
        config->ipc.videoSocketPath, origins, QStringLiteral("ipc.video_socket"));
    config->ipc.analysisSocketPath = readString(ipc, QStringLiteral("analysis_socket"),
        config->ipc.analysisSocketPath, origins, QStringLiteral("ipc.analysis_socket"));
    config->ipc.fallSocketPath = readString(ipc, QStringLiteral("fall_socket"),
        config->ipc.fallSocketPath, origins, QStringLiteral("ipc.fall_socket"));
    config->ipc.analysisSharedMemoryName = readString(ipc, QStringLiteral("analysis_shared_memory_name"),
        config->ipc.analysisSharedMemoryName, origins, QStringLiteral("ipc.analysis_shared_memory_name"));

    const QJsonObject video = root.value(QStringLiteral("video")).toObject();
    config->video.cameraId = readString(video, QStringLiteral("camera_id"),
        config->video.cameraId, origins, QStringLiteral("video.camera_id"));
    config->video.devicePath = readString(video, QStringLiteral("device_path"),
        config->video.devicePath, origins, QStringLiteral("video.device_path"));
    config->video.pipelineBackend = readString(video, QStringLiteral("pipeline_backend"),
        config->video.pipelineBackend, origins, QStringLiteral("video.pipeline_backend"));
    config->video.analysisEnabled = readBool(video, QStringLiteral("analysis_enabled"),
        config->video.analysisEnabled, origins, QStringLiteral("video.analysis_enabled"));
    config->video.analysisConvertBackend = readString(video, QStringLiteral("analysis_convert_backend"),
        config->video.analysisConvertBackend, origins, QStringLiteral("video.analysis_convert_backend"));
    config->video.gstLaunchBin = readString(video, QStringLiteral("gst_launch_bin"),
        config->video.gstLaunchBin, origins, QStringLiteral("video.gst_launch_bin"));

    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    config->analysis.transport = readString(analysis, QStringLiteral("transport"),
        config->analysis.transport, origins, QStringLiteral("analysis.transport"));
    config->analysis.dmaHeap = readString(analysis, QStringLiteral("dma_heap"),
        config->analysis.dmaHeap, origins, QStringLiteral("analysis.dma_heap"));
    config->analysis.rgaOutputDmabuf = readBool(analysis, QStringLiteral("rga_output_dmabuf"),
        config->analysis.rgaOutputDmabuf, origins, QStringLiteral("analysis.rga_output_dmabuf"));
    config->analysis.gstDmabufInput = readBool(analysis, QStringLiteral("gst_dmabuf_input"),
        config->analysis.gstDmabufInput, origins, QStringLiteral("analysis.gst_dmabuf_input"));
    config->analysis.gstForceDmabufIo = readBool(analysis, QStringLiteral("gst_force_dmabuf_io"),
        config->analysis.gstForceDmabufIo, origins, QStringLiteral("analysis.gst_force_dmabuf_io"));

    const QJsonObject fall = root.value(QStringLiteral("fall_detection")).toObject();
    config->fallDetection.enabled = readBool(fall, QStringLiteral("enabled"),
        config->fallDetection.enabled, origins, QStringLiteral("fall_detection.enabled"));
    config->fallDetection.poseModelPath = readString(fall, QStringLiteral("pose_model_path"),
        config->fallDetection.poseModelPath, origins, QStringLiteral("fall_detection.pose_model_path"));
    config->fallDetection.stgcnModelPath = readString(fall, QStringLiteral("stgcn_model_path"),
        config->fallDetection.stgcnModelPath, origins, QStringLiteral("fall_detection.stgcn_model_path"));
    config->fallDetection.lstmModelPath = readString(fall, QStringLiteral("lstm_model_path"),
        config->fallDetection.lstmModelPath, origins, QStringLiteral("fall_detection.lstm_model_path"));
    config->fallDetection.lstmWeightsPath = readString(fall, QStringLiteral("lstm_weights_path"),
        config->fallDetection.lstmWeightsPath, origins, QStringLiteral("fall_detection.lstm_weights_path"));
    config->fallDetection.actionModelPath = readString(fall, QStringLiteral("action_model_path"),
        config->fallDetection.actionModelPath, origins, QStringLiteral("fall_detection.action_model_path"));
    config->fallDetection.actionBackend = readString(fall, QStringLiteral("action_backend"),
        config->fallDetection.actionBackend, origins, QStringLiteral("fall_detection.action_backend"));
    config->fallDetection.maxTracks = readInt(fall, QStringLiteral("max_tracks"),
        config->fallDetection.maxTracks, origins, QStringLiteral("fall_detection.max_tracks"));
    config->fallDetection.trackHighThresh = readDouble(fall, QStringLiteral("track_high_thresh"),
        config->fallDetection.trackHighThresh, origins, QStringLiteral("fall_detection.track_high_thresh"));
    config->fallDetection.trackLowThresh = readDouble(fall, QStringLiteral("track_low_thresh"),
        config->fallDetection.trackLowThresh, origins, QStringLiteral("fall_detection.track_low_thresh"));
    config->fallDetection.newTrackThresh = readDouble(fall, QStringLiteral("new_track_thresh"),
        config->fallDetection.newTrackThresh, origins, QStringLiteral("fall_detection.new_track_thresh"));
    config->fallDetection.matchThresh = readDouble(fall, QStringLiteral("match_thresh"),
        config->fallDetection.matchThresh, origins, QStringLiteral("fall_detection.match_thresh"));
    config->fallDetection.lostTimeoutMs = readInt(fall, QStringLiteral("lost_timeout_ms"),
        config->fallDetection.lostTimeoutMs, origins, QStringLiteral("fall_detection.lost_timeout_ms"));
    config->fallDetection.minValidKeypoints = readInt(fall, QStringLiteral("min_valid_keypoints"),
        config->fallDetection.minValidKeypoints, origins, QStringLiteral("fall_detection.min_valid_keypoints"));
    config->fallDetection.minBoxArea = readDouble(fall, QStringLiteral("min_box_area"),
        config->fallDetection.minBoxArea, origins, QStringLiteral("fall_detection.min_box_area"));
    config->fallDetection.sequenceLength = readInt(fall, QStringLiteral("sequence_length"),
        config->fallDetection.sequenceLength, origins, QStringLiteral("fall_detection.sequence_length"));
    config->fallDetection.rknnInputDmabuf = readBool(fall, QStringLiteral("rknn_input_dmabuf"),
        config->fallDetection.rknnInputDmabuf, origins, QStringLiteral("fall_detection.rknn_input_dmabuf"));
    config->fallDetection.rknnIoMemMode = readString(fall, QStringLiteral("rknn_io_mem_mode"),
        config->fallDetection.rknnIoMemMode, origins, QStringLiteral("fall_detection.rknn_io_mem_mode"));

    const QJsonObject debug = root.value(QStringLiteral("debug")).toObject();
    config->debug.healthdEventMarkerPath = readString(debug, QStringLiteral("healthd_event_marker_path"),
        config->debug.healthdEventMarkerPath, origins, QStringLiteral("debug.healthd_event_marker_path"));
    config->debug.videoLatencyMarkerPath = readString(debug, QStringLiteral("video_latency_marker_path"),
        config->debug.videoLatencyMarkerPath, origins, QStringLiteral("debug.video_latency_marker_path"));
    config->debug.fallLatencyMarkerPath = readString(debug, QStringLiteral("fall_latency_marker_path"),
        config->debug.fallLatencyMarkerPath, origins, QStringLiteral("debug.fall_latency_marker_path"));
    config->debug.fallPoseTimingPath = readString(debug, QStringLiteral("fall_pose_timing_path"),
        config->debug.fallPoseTimingPath, origins, QStringLiteral("debug.fall_pose_timing_path"));
    config->debug.fallTrackTracePath = readString(debug, QStringLiteral("fall_track_trace_path"),
        config->debug.fallTrackTracePath, origins, QStringLiteral("debug.fall_track_trace_path"));
    config->debug.fallActionDebug = readBool(debug, QStringLiteral("fall_action_debug"),
        config->debug.fallActionDebug, origins, QStringLiteral("debug.fall_action_debug"));
    config->debug.fallLstmTracePath = readString(debug, QStringLiteral("fall_lstm_trace_path"),
        config->debug.fallLstmTracePath, origins, QStringLiteral("debug.fall_lstm_trace_path"));
}

void applyEnvironmentOverrides(AppRuntimeConfig *config, QHash<QString, QString> *origins) {
    if (!config) {
        return;
    }

    auto applyString = [origins](const char *envName, QString *target, const QString &originKey) {
        const QString value = qEnvironmentVariable(envName).trimmed();
        if (value.isEmpty() || !target) {
            return;
        }
        *target = value;
        markOrigin(origins, originKey, QStringLiteral("environment"));
    };
    auto applyInt = [origins](const char *envName, int *target, const QString &originKey) {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue(envName, &ok);
        if (!ok || !target) {
            return;
        }
        *target = value;
        markOrigin(origins, originKey, QStringLiteral("environment"));
    };
    auto applyDouble = [origins](const char *envName, double *target, const QString &originKey) {
        bool ok = false;
        const double value = qEnvironmentVariable(envName).toDouble(&ok);
        if (!ok || !target) {
            return;
        }
        *target = value;
        markOrigin(origins, originKey, QStringLiteral("environment"));
    };
    auto applyBool = [origins](const char *envName, bool *target, const QString &originKey) {
        bool applied = false;
        const bool value = envBool(envName, target ? *target : false, &applied);
        if (!applied || !target) {
            return;
        }
        *target = value;
        markOrigin(origins, originKey, QStringLiteral("environment"));
    };

    applyString("RK_RUNTIME_MODE", &config->system.runtimeMode, QStringLiteral("system.runtime_mode"));
    applyString("HEALTHD_DB_PATH", &config->paths.databasePath, QStringLiteral("paths.database_path"));
    applyString("RK_HEALTH_STATION_SOCKET_NAME", &config->ipc.healthSocketPath, QStringLiteral("ipc.health_socket"));
    applyString("RK_VIDEO_SOCKET_NAME", &config->ipc.videoSocketPath, QStringLiteral("ipc.video_socket"));
    applyString("RK_VIDEO_ANALYSIS_SOCKET_PATH", &config->ipc.analysisSocketPath, QStringLiteral("ipc.analysis_socket"));
    applyString("RK_FALL_SOCKET_NAME", &config->ipc.fallSocketPath, QStringLiteral("ipc.fall_socket"));
    applyString("RK_VIDEO_ANALYSIS_SHM_NAME", &config->ipc.analysisSharedMemoryName, QStringLiteral("ipc.analysis_shared_memory_name"));
    applyString("RK_FALL_CAMERA_ID", &config->video.cameraId, QStringLiteral("video.camera_id"));
    applyString("RK_VIDEO_CAMERA_ID", &config->video.cameraId, QStringLiteral("video.camera_id"));
    applyString("RK_VIDEO_DEVICE_PATH", &config->video.devicePath, QStringLiteral("video.device_path"));
    applyString("RK_VIDEO_PIPELINE_BACKEND", &config->video.pipelineBackend, QStringLiteral("video.pipeline_backend"));
    applyString("RK_VIDEO_ANALYSIS_CONVERT_BACKEND", &config->video.analysisConvertBackend, QStringLiteral("video.analysis_convert_backend"));
    applyString("RK_VIDEO_GST_LAUNCH_BIN", &config->video.gstLaunchBin, QStringLiteral("video.gst_launch_bin"));
    applyBool("RK_VIDEO_ANALYSIS_ENABLED", &config->video.analysisEnabled, QStringLiteral("video.analysis_enabled"));
    applyString("RK_VIDEO_ANALYSIS_TRANSPORT", &config->analysis.transport, QStringLiteral("analysis.transport"));
    applyString("RK_VIDEO_ANALYSIS_DMA_HEAP", &config->analysis.dmaHeap, QStringLiteral("analysis.dma_heap"));
    applyBool("RK_VIDEO_RGA_OUTPUT_DMABUF", &config->analysis.rgaOutputDmabuf, QStringLiteral("analysis.rga_output_dmabuf"));
    applyBool("RK_VIDEO_GST_DMABUF_INPUT", &config->analysis.gstDmabufInput, QStringLiteral("analysis.gst_dmabuf_input"));
    applyBool("RK_VIDEO_GST_FORCE_DMABUF_IO", &config->analysis.gstForceDmabufIo, QStringLiteral("analysis.gst_force_dmabuf_io"));
    applyBool("RK_VIDEO_ANALYSIS_ENABLED", &config->fallDetection.enabled, QStringLiteral("fall_detection.enabled"));
    applyString("RK_FALL_POSE_MODEL_PATH", &config->fallDetection.poseModelPath, QStringLiteral("fall_detection.pose_model_path"));
    applyString("RK_FALL_STGCN_MODEL_PATH", &config->fallDetection.stgcnModelPath, QStringLiteral("fall_detection.stgcn_model_path"));
    applyString("RK_FALL_LSTM_MODEL_PATH", &config->fallDetection.lstmModelPath, QStringLiteral("fall_detection.lstm_model_path"));
    applyString("RK_FALL_LSTM_WEIGHTS_PATH", &config->fallDetection.lstmWeightsPath, QStringLiteral("fall_detection.lstm_weights_path"));
    applyString("RK_FALL_ACTION_MODEL_PATH", &config->fallDetection.actionModelPath, QStringLiteral("fall_detection.action_model_path"));
    applyString("RK_FALL_ACTION_BACKEND", &config->fallDetection.actionBackend, QStringLiteral("fall_detection.action_backend"));
    applyInt("RK_FALL_MAX_TRACKS", &config->fallDetection.maxTracks, QStringLiteral("fall_detection.max_tracks"));
    applyDouble("RK_FALL_TRACK_HIGH_THRESH", &config->fallDetection.trackHighThresh, QStringLiteral("fall_detection.track_high_thresh"));
    applyDouble("RK_FALL_TRACK_LOW_THRESH", &config->fallDetection.trackLowThresh, QStringLiteral("fall_detection.track_low_thresh"));
    applyDouble("RK_FALL_NEW_TRACK_THRESH", &config->fallDetection.newTrackThresh, QStringLiteral("fall_detection.new_track_thresh"));
    applyDouble("RK_FALL_MATCH_THRESH", &config->fallDetection.matchThresh, QStringLiteral("fall_detection.match_thresh"));
    applyInt("RK_FALL_LOST_TIMEOUT_MS", &config->fallDetection.lostTimeoutMs, QStringLiteral("fall_detection.lost_timeout_ms"));
    applyInt("RK_FALL_MIN_VALID_KEYPOINTS", &config->fallDetection.minValidKeypoints, QStringLiteral("fall_detection.min_valid_keypoints"));
    applyDouble("RK_FALL_MIN_BOX_AREA", &config->fallDetection.minBoxArea, QStringLiteral("fall_detection.min_box_area"));
    applyInt("RK_FALL_SEQUENCE_LENGTH", &config->fallDetection.sequenceLength, QStringLiteral("fall_detection.sequence_length"));
    applyBool("RK_FALL_RKNN_INPUT_DMABUF", &config->fallDetection.rknnInputDmabuf, QStringLiteral("fall_detection.rknn_input_dmabuf"));
    applyString("RK_FALL_RKNN_IO_MEM", &config->fallDetection.rknnIoMemMode, QStringLiteral("fall_detection.rknn_io_mem_mode"));
    applyString("HEALTHD_EVENT_MARKER_PATH", &config->debug.healthdEventMarkerPath, QStringLiteral("debug.healthd_event_marker_path"));
    applyString("RK_VIDEO_LATENCY_MARKER_PATH", &config->debug.videoLatencyMarkerPath, QStringLiteral("debug.video_latency_marker_path"));
    applyString("RK_FALL_LATENCY_MARKER_PATH", &config->debug.fallLatencyMarkerPath, QStringLiteral("debug.fall_latency_marker_path"));
    applyString("RK_FALL_POSE_TIMING_PATH", &config->debug.fallPoseTimingPath, QStringLiteral("debug.fall_pose_timing_path"));
    applyString("RK_FALL_TRACK_TRACE_PATH", &config->debug.fallTrackTracePath, QStringLiteral("debug.fall_track_trace_path"));
    applyBool("RK_FALL_ACTION_DEBUG", &config->debug.fallActionDebug, QStringLiteral("debug.fall_action_debug"));
    applyString("RK_FALL_LSTM_TRACE_PATH", &config->debug.fallLstmTracePath, QStringLiteral("debug.fall_lstm_trace_path"));
}
}

LoadedAppRuntimeConfig loadAppRuntimeConfig(const QString &explicitPath) {
    LoadedAppRuntimeConfig result;
    result.config = buildDefaultAppRuntimeConfig();
    result.origins = buildDefaultRuntimeConfigOrigins();
    result.configPath = resolveRuntimeConfigPath(explicitPath);

    if (!result.configPath.isEmpty()) {
        QFile file(result.configPath);
        if (!file.exists()) {
            result.errors.append(QStringLiteral("runtime config file does not exist: %1").arg(result.configPath));
        } else if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.errors.append(QStringLiteral("failed to open runtime config: %1").arg(result.configPath));
        } else {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                result.errors.append(QStringLiteral("invalid runtime config json: %1").arg(parseError.errorString()));
            } else {
                applyJson(doc.object(), &result.config, &result.origins);
            }
        }
    }

    applyEnvironmentOverrides(&result.config, &result.origins);
    normalizeRuntimeConfigPaths(result.configPath, &result.config);
    validateAppRuntimeConfig(result.config, &result.errors, &result.warnings);
    result.ok = result.errors.isEmpty();
    return result;
}
