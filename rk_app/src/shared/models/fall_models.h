#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class AnalysisPixelFormat {
    Jpeg = 0,
    Nv12 = 1,
    Rgb = 2,
};

struct AnalysisFramePacket {
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    QString cameraId;
    qint32 width = 0;
    qint32 height = 0;
    AnalysisPixelFormat pixelFormat = AnalysisPixelFormat::Jpeg;
    QByteArray payload;
};

struct AnalysisFrameDescriptor {
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    QString cameraId;
    qint32 width = 0;
    qint32 height = 0;
    AnalysisPixelFormat pixelFormat = AnalysisPixelFormat::Jpeg;
    quint32 slotIndex = 0;
    quint64 sequence = 0;
    quint32 payloadBytes = 0;
};

struct AnalysisChannelStatus {
    QString cameraId;
    bool enabled = false;
    bool streamConnected = false;
    QString outputFormat;
    int width = 0;
    int height = 0;
    int fps = 0;
    quint64 droppedFrames = 0;
    QString lastError;
};

struct FallRuntimeStatus {
    QString cameraId;
    bool inputConnected = false;
    bool poseModelReady = false;
    bool actionModelReady = false;
    double currentFps = 0.0;
    qint64 lastFrameTs = 0;
    qint64 lastInferTs = 0;
    QString latestState;
    double latestConfidence = 0.0;
    QString lastError;
};

struct FallEvent {
    QString eventId;
    QString cameraId;
    qint64 tsStart = 0;
    qint64 tsConfirm = 0;
    QString eventType;
    double confidence = 0.0;
    QString snapshotRef;
    QString clipRef;
};

struct FallClassificationResult {
    QString cameraId;
    qint64 timestampMs = 0;
    QString state;
    double confidence = 0.0;
};

struct FallClassificationEntry {
    int trackId = -1;
    int iconId = -1;
    QString state;
    double confidence = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    double bboxX = 0.0;
    double bboxY = 0.0;
    double bboxW = 0.0;
    double bboxH = 0.0;
};

struct FallClassificationBatch {
    QString cameraId;
    qint64 timestampMs = 0;
    QVector<FallClassificationEntry> results;
};

Q_DECLARE_METATYPE(AnalysisFramePacket)
Q_DECLARE_METATYPE(AnalysisFrameDescriptor)
Q_DECLARE_METATYPE(FallClassificationResult)
Q_DECLARE_METATYPE(FallClassificationBatch)
