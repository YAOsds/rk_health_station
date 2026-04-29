#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

struct SystemRuntimeConfig {
    QString runtimeMode = QStringLiteral("auto");
};

struct PathsRuntimeConfig {
    QString storageDir = QStringLiteral("/home/elf/videosurv/");
    QString databasePath = QStringLiteral("./data/healthd.sqlite");
};

struct IpcRuntimeConfig {
    QString healthSocketPath = QStringLiteral("./run/rk_health_station.sock");
    QString videoSocketPath = QStringLiteral("./run/rk_video.sock");
    QString analysisSocketPath = QStringLiteral("./run/rk_video_analysis.sock");
    QString fallSocketPath = QStringLiteral("./run/rk_fall.sock");
    QString analysisSharedMemoryName;
};

struct VideoRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString devicePath = QStringLiteral("/dev/video11");
    QString pipelineBackend = QStringLiteral("process");
    bool analysisEnabled = true;
    QString analysisConvertBackend = QStringLiteral("gstreamer_cpu");
    QString gstLaunchBin = QStringLiteral("gst-launch-1.0");
};

struct AnalysisRuntimeConfig {
    QString transport = QStringLiteral("shared_memory");
    QString dmaHeap = QStringLiteral("/dev/dma_heap/system-uncached-dma32");
    bool rgaOutputDmabuf = false;
    bool gstDmabufInput = false;
    bool gstForceDmabufIo = false;
};

struct FallDetectionRuntimeConfig {
    bool enabled = true;
    QString poseModelPath = QStringLiteral("assets/models/yolov8n-pose.rknn");
    QString stgcnModelPath = QStringLiteral("assets/models/stgcn_fall.rknn");
    QString lstmModelPath = QStringLiteral("assets/models/lstm_fall.rknn");
    QString lstmWeightsPath;
    QString actionModelPath = QStringLiteral("assets/models/stgcn_fall.onnx");
    QString actionBackend = QStringLiteral("lstm_rknn");
    int maxTracks = 5;
    double trackHighThresh = 0.35;
    double trackLowThresh = 0.10;
    double newTrackThresh = 0.45;
    double matchThresh = 0.80;
    int lostTimeoutMs = 800;
    int minValidKeypoints = 8;
    double minBoxArea = 4096.0;
    int sequenceLength = 45;
    bool rknnInputDmabuf = false;
    QString rknnIoMemMode = QStringLiteral("default");
};

struct DebugRuntimeConfig {
    QString healthdEventMarkerPath;
    QString videoLatencyMarkerPath;
    QString fallLatencyMarkerPath;
    QString fallPoseTimingPath;
    QString fallTrackTracePath;
    bool fallActionDebug = false;
    QString fallLstmTracePath;
};

struct AppRuntimeConfig {
    SystemRuntimeConfig system;
    PathsRuntimeConfig paths;
    IpcRuntimeConfig ipc;
    VideoRuntimeConfig video;
    AnalysisRuntimeConfig analysis;
    FallDetectionRuntimeConfig fallDetection;
    DebugRuntimeConfig debug;
};

struct LoadedAppRuntimeConfig {
    bool ok = false;
    AppRuntimeConfig config;
    QStringList errors;
    QStringList warnings;
    QHash<QString, QString> origins;
    QString configPath;
};

AppRuntimeConfig buildDefaultAppRuntimeConfig();
QHash<QString, QString> buildDefaultRuntimeConfigOrigins();
