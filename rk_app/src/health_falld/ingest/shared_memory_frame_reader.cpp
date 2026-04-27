#include "ingest/shared_memory_frame_reader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

SharedMemoryFrameReader::SharedMemoryFrameReader(const QString &sharedMemoryNameOverride)
    : sharedMemoryNameOverride_(sharedMemoryNameOverride) {
}

SharedMemoryFrameReader::~SharedMemoryFrameReader() {
    cleanup();
}

bool SharedMemoryFrameReader::read(
    const AnalysisFrameDescriptor &descriptor, AnalysisFramePacket *packet, QString *error) {
    if (error) {
        error->clear();
    }
    if (!packet) {
        if (error) {
            *error = QStringLiteral("analysis_packet_null");
        }
        return false;
    }
    if (!ensureMapped(descriptor.cameraId, error)) {
        return false;
    }
    if (descriptor.slotIndex >= header_->slotCount) {
        if (error) {
            *error = QStringLiteral("analysis_slot_out_of_range");
        }
        return false;
    }

    const SharedFrameSlotHeader *slotHeader = slotHeaderFor(descriptor.slotIndex);
    const quint64 firstSequence = __atomic_load_n(&slotHeader->sequence, __ATOMIC_ACQUIRE);
    if (firstSequence != descriptor.sequence) {
        if (error) {
            *error = QStringLiteral("analysis_slot_overwritten");
        }
        return false;
    }
    if ((firstSequence & 1ULL) != 0ULL) {
        if (error) {
            *error = QStringLiteral("analysis_slot_incomplete");
        }
        return false;
    }
    if (slotHeader->payloadBytes != descriptor.payloadBytes
        || slotHeader->payloadBytes > header_->maxFrameBytes) {
        if (error) {
            *error = QStringLiteral("analysis_payload_invalid");
        }
        return false;
    }

    packet->frameId = slotHeader->frameId;
    packet->timestampMs = slotHeader->timestampMs;
    packet->cameraId = descriptor.cameraId;
    packet->width = slotHeader->width;
    packet->height = slotHeader->height;
    packet->pixelFormat = static_cast<AnalysisPixelFormat>(slotHeader->pixelFormat);
    packet->posePreprocessed = (slotHeader->flags & 1u) != 0u;
    packet->poseXPad = slotHeader->poseXPad;
    packet->poseYPad = slotHeader->poseYPad;
    packet->poseScale = slotHeader->poseScale;
    packet->payload = QByteArray(slotPayloadFor(descriptor.slotIndex), slotHeader->payloadBytes);

    const quint64 secondSequence = __atomic_load_n(&slotHeader->sequence, __ATOMIC_ACQUIRE);
    if (secondSequence != firstSequence) {
        if (error) {
            *error = QStringLiteral("analysis_slot_rewritten_during_read");
        }
        return false;
    }
    return true;
}

bool SharedMemoryFrameReader::ensureMapped(const QString &cameraId, QString *error) {
    const QString desiredShmName = sharedMemoryNameOverride_.isEmpty()
        ? sharedMemoryNameForCamera(cameraId)
        : sharedMemoryNameOverride_;

    if (cameraId_ == cameraId && header_) {
        struct stat statBuffer;
        if (fd_ >= 0 && ::fstat(fd_, &statBuffer) == 0 && statBuffer.st_nlink > 0) {
            return true;
        }

        // The writer recreates the same-named ring when preview/test pipelines restart.
        // Once the old object is unlinked, this reader must reopen the new ring instead
        // of continuing to read stale pages from the deleted mapping.
        cleanup();
    }

    cleanup();
    shmName_ = desiredShmName;
    fd_ = ::shm_open(shmName_.toUtf8().constData(), O_RDONLY, 0);
    if (fd_ < 0) {
        if (error) {
            *error = QStringLiteral("analysis_ring_open_failed");
        }
        return false;
    }

    struct stat statBuffer;
    if (::fstat(fd_, &statBuffer) != 0 || statBuffer.st_size <= 0) {
        if (error) {
            *error = QStringLiteral("analysis_ring_stat_failed");
        }
        cleanup();
        return false;
    }

    mapBytes_ = static_cast<qsizetype>(statBuffer.st_size);
    mapped_ = ::mmap(nullptr, mapBytes_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mapped_ == MAP_FAILED) {
        mapped_ = nullptr;
        if (error) {
            *error = QStringLiteral("analysis_ring_map_failed");
        }
        cleanup();
        return false;
    }

    if (mapBytes_ < static_cast<qsizetype>(sizeof(SharedFrameRingHeader))) {
        if (error) {
            *error = QStringLiteral("analysis_ring_invalid_header");
        }
        cleanup();
        return false;
    }

    header_ = static_cast<SharedFrameRingHeader *>(mapped_);
    const qsizetype minimumSlotStride = static_cast<qsizetype>(sizeof(SharedFrameSlotHeader));
    const qsizetype declaredSlotStride = static_cast<qsizetype>(header_->slotStride);
    const qsizetype declaredSlotCount = static_cast<qsizetype>(header_->slotCount);
    const qsizetype declaredMaxFrameBytes = static_cast<qsizetype>(header_->maxFrameBytes);
    const bool headerIsValid = header_->magic == SharedFrameRingHeader().magic
        && header_->version == 1
        && declaredSlotCount > 0
        && declaredSlotStride >= minimumSlotStride
        && declaredMaxFrameBytes <= declaredSlotStride - minimumSlotStride
        && declaredSlotCount <= (mapBytes_ - static_cast<qsizetype>(sizeof(SharedFrameRingHeader))) / declaredSlotStride;
    if (!headerIsValid) {
        if (error) {
            *error = QStringLiteral("analysis_ring_invalid_header");
        }
        cleanup();
        return false;
    }

    cameraId_ = cameraId;
    return true;
}

const SharedFrameSlotHeader *SharedMemoryFrameReader::slotHeaderFor(quint32 slotIndex) const {
    const char *base = static_cast<const char *>(mapped_) + sizeof(SharedFrameRingHeader)
        + static_cast<qsizetype>(header_->slotStride) * slotIndex;
    return reinterpret_cast<const SharedFrameSlotHeader *>(base);
}

const char *SharedMemoryFrameReader::slotPayloadFor(quint32 slotIndex) const {
    return reinterpret_cast<const char *>(slotHeaderFor(slotIndex) + 1);
}

void SharedMemoryFrameReader::cleanup() {
    if (mapped_) {
        ::munmap(mapped_, mapBytes_);
        mapped_ = nullptr;
    }
    header_ = nullptr;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    mapBytes_ = 0;
    cameraId_.clear();
    shmName_.clear();
}
