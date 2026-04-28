#include "runtime_config/app_runtime_config.h"

AppRuntimeConfig buildDefaultAppRuntimeConfig() {
    return AppRuntimeConfig();
}

QHash<QString, QString> buildDefaultRuntimeConfigOrigins() {
    return {
        {QStringLiteral("system.runtime_mode"), QStringLiteral("default")},
        {QStringLiteral("paths.storage_dir"), QStringLiteral("default")},
        {QStringLiteral("paths.database_path"), QStringLiteral("default")},
        {QStringLiteral("ipc.health_socket"), QStringLiteral("default")},
        {QStringLiteral("ipc.video_socket"), QStringLiteral("default")},
        {QStringLiteral("ipc.analysis_socket"), QStringLiteral("default")},
        {QStringLiteral("ipc.fall_socket"), QStringLiteral("default")},
        {QStringLiteral("ipc.analysis_shared_memory_name"), QStringLiteral("default")},
        {QStringLiteral("video.camera_id"), QStringLiteral("default")},
        {QStringLiteral("video.device_path"), QStringLiteral("default")},
        {QStringLiteral("video.pipeline_backend"), QStringLiteral("default")},
        {QStringLiteral("video.analysis_enabled"), QStringLiteral("default")},
        {QStringLiteral("video.analysis_convert_backend"), QStringLiteral("default")},
        {QStringLiteral("video.gst_launch_bin"), QStringLiteral("default")},
        {QStringLiteral("analysis.transport"), QStringLiteral("default")},
        {QStringLiteral("analysis.dma_heap"), QStringLiteral("default")},
        {QStringLiteral("analysis.rga_output_dmabuf"), QStringLiteral("default")},
        {QStringLiteral("analysis.gst_dmabuf_input"), QStringLiteral("default")},
        {QStringLiteral("analysis.gst_force_dmabuf_io"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.enabled"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.pose_model_path"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.stgcn_model_path"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.lstm_model_path"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.lstm_weights_path"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.action_model_path"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.action_backend"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.max_tracks"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.track_high_thresh"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.track_low_thresh"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.new_track_thresh"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.match_thresh"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.lost_timeout_ms"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.min_valid_keypoints"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.min_box_area"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.sequence_length"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.rknn_input_dmabuf"), QStringLiteral("default")},
        {QStringLiteral("fall_detection.rknn_io_mem_mode"), QStringLiteral("default")},
        {QStringLiteral("debug.healthd_event_marker_path"), QStringLiteral("default")},
        {QStringLiteral("debug.video_latency_marker_path"), QStringLiteral("default")},
        {QStringLiteral("debug.fall_latency_marker_path"), QStringLiteral("default")},
        {QStringLiteral("debug.fall_pose_timing_path"), QStringLiteral("default")},
        {QStringLiteral("debug.fall_track_trace_path"), QStringLiteral("default")},
        {QStringLiteral("debug.fall_action_debug"), QStringLiteral("default")},
        {QStringLiteral("debug.fall_lstm_trace_path"), QStringLiteral("default")},
    };
}
