#pragma once

#include "analysis/shared_memory_frame_ring.h"

class SharedMemoryFrameReader {
public:
    explicit SharedMemoryFrameReader(const QString &sharedMemoryNameOverride = QString());
    ~SharedMemoryFrameReader();

    bool read(const AnalysisFrameDescriptor &descriptor,
        AnalysisFramePacket *packet, QString *error);

private:
    bool ensureMapped(const QString &cameraId, QString *error);
    const SharedFrameSlotHeader *slotHeaderFor(quint32 slotIndex) const;
    const char *slotPayloadFor(quint32 slotIndex) const;
    void cleanup();

    QString cameraId_;
    QString shmName_;
    QString sharedMemoryNameOverride_;
    int fd_ = -1;
    qsizetype mapBytes_ = 0;
    void *mapped_ = nullptr;
    SharedFrameRingHeader *header_ = nullptr;
};
