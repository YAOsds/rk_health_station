#pragma once

#include "models/fall_models.h"

#include <QByteArray>
#include <QString>

struct SharedFrameRingHeader {
    quint32 magic = 0x524B5348; // RKSH
    quint16 version = 1;
    quint16 slotCount = 0;
    quint32 slotStride = 0;
    quint32 maxFrameBytes = 0;
    quint32 producerPid = 0;
    quint64 publishedFrames = 0;
    quint64 droppedFrames = 0;
};

struct SharedFrameSlotHeader {
    quint64 sequence = 0;
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    qint32 width = 0;
    qint32 height = 0;
    qint32 pixelFormat = 0;
    quint32 payloadBytes = 0;
    quint32 flags = 0;
    qint32 poseXPad = 0;
    qint32 poseYPad = 0;
    float poseScale = 1.0f;
};

struct SharedFramePublishResult {
    quint32 slotIndex = 0;
    quint64 sequence = 0;
    quint32 payloadBytes = 0;
};

inline QString sharedMemoryNameForCamera(const QString &cameraId) {
    const QByteArray encoded = cameraId.toUtf8().toHex();
    return QStringLiteral("/rk_video_analysis_%1").arg(QString::fromLatin1(encoded));
}

class SharedMemoryFrameRingWriter {
public:
    SharedMemoryFrameRingWriter(const QString &cameraId, quint16 slotCount, quint32 maxFrameBytes);
    ~SharedMemoryFrameRingWriter();

    bool initialize(QString *error = nullptr);
    SharedFramePublishResult publish(const AnalysisFramePacket &frame);
    quint64 droppedFrames() const;

private:
    SharedFrameSlotHeader *slotHeaderFor(quint32 slotIndex) const;
    char *slotPayloadFor(quint32 slotIndex) const;
    void cleanup();

    QString cameraId_;
    QString shmName_;
    quint16 slotCount_ = 0;
    quint32 maxFrameBytes_ = 0;
    int fd_ = -1;
    qsizetype mapBytes_ = 0;
    void *mapped_ = nullptr;
    SharedFrameRingHeader *header_ = nullptr;
    quint32 nextSlotIndex_ = 0;
};
