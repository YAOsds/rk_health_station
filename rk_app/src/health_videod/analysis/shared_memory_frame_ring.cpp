#include "analysis/shared_memory_frame_ring.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr mode_t kSharedMemoryMode = 0600;
}

SharedMemoryFrameRingWriter::SharedMemoryFrameRingWriter(
    const QString &cameraId, quint16 slotCount, quint32 maxFrameBytes)
    : cameraId_(cameraId)
    , shmName_(sharedMemoryNameForCamera(cameraId))
    , slotCount_(slotCount)
    , maxFrameBytes_(maxFrameBytes) {
}

SharedMemoryFrameRingWriter::~SharedMemoryFrameRingWriter() {
    cleanup();
}

bool SharedMemoryFrameRingWriter::initialize(QString *error) {
    if (error) {
        error->clear();
    }
    if (mapped_) {
        return true;
    }
    if (cameraId_.isEmpty() || slotCount_ == 0 || maxFrameBytes_ == 0) {
        if (error) {
            *error = QStringLiteral("analysis_ring_invalid_config");
        }
        return false;
    }

    ::shm_unlink(shmName_.toUtf8().constData());
    fd_ = ::shm_open(shmName_.toUtf8().constData(), O_CREAT | O_RDWR, kSharedMemoryMode);
    if (fd_ < 0) {
        if (error) {
            *error = QStringLiteral("analysis_ring_open_failed");
        }
        return false;
    }

    const quint32 slotStride = static_cast<quint32>(sizeof(SharedFrameSlotHeader)) + maxFrameBytes_;
    mapBytes_ = static_cast<qsizetype>(sizeof(SharedFrameRingHeader))
        + static_cast<qsizetype>(slotStride) * slotCount_;
    if (::ftruncate(fd_, mapBytes_) != 0) {
        if (error) {
            *error = QStringLiteral("analysis_ring_resize_failed");
        }
        cleanup();
        return false;
    }

    mapped_ = ::mmap(nullptr, mapBytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_ == MAP_FAILED) {
        mapped_ = nullptr;
        if (error) {
            *error = QStringLiteral("analysis_ring_map_failed");
        }
        cleanup();
        return false;
    }

    memset(mapped_, 0, mapBytes_);
    header_ = static_cast<SharedFrameRingHeader *>(mapped_);
    header_->magic = SharedFrameRingHeader().magic;
    header_->version = SharedFrameRingHeader().version;
    header_->slotCount = slotCount_;
    header_->slotStride = slotStride;
    header_->maxFrameBytes = maxFrameBytes_;
    header_->producerPid = static_cast<quint32>(::getpid());
    return true;
}

SharedFramePublishResult SharedMemoryFrameRingWriter::publish(const AnalysisFramePacket &frame) {
    SharedFramePublishResult result;
    if (!header_ || frame.payload.isEmpty()
        || frame.payload.size() > static_cast<int>(header_->maxFrameBytes)) {
        if (header_) {
            header_->droppedFrames += 1;
        }
        return result;
    }

    const quint32 slotIndex = nextSlotIndex_++ % header_->slotCount;
    SharedFrameSlotHeader *slotHeader = slotHeaderFor(slotIndex);
    char *slotPayload = slotPayloadFor(slotIndex);
    const quint64 previousCommittedSequence =
        __atomic_load_n(&slotHeader->sequence, __ATOMIC_RELAXED);
    const quint64 writeSequence
        = (previousCommittedSequence & 1ULL) == 0ULL
        ? previousCommittedSequence + 1ULL
        : previousCommittedSequence + 2ULL;
    const quint64 committedSequence = writeSequence + 1ULL;

    __atomic_store_n(&slotHeader->sequence, writeSequence, __ATOMIC_RELAXED);

    memcpy(slotPayload, frame.payload.constData(), static_cast<size_t>(frame.payload.size()));
    slotHeader->frameId = frame.frameId;
    slotHeader->timestampMs = frame.timestampMs;
    slotHeader->width = frame.width;
    slotHeader->height = frame.height;
    slotHeader->pixelFormat = static_cast<qint32>(frame.pixelFormat);
    slotHeader->payloadBytes = static_cast<quint32>(frame.payload.size());
    slotHeader->flags = frame.posePreprocessed ? 1u : 0u;
    slotHeader->poseXPad = frame.poseXPad;
    slotHeader->poseYPad = frame.poseYPad;
    slotHeader->poseScale = frame.poseScale;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&slotHeader->sequence, committedSequence, __ATOMIC_RELEASE);

    header_->publishedFrames += 1;
    result.slotIndex = slotIndex;
    result.sequence = committedSequence;
    result.payloadBytes = slotHeader->payloadBytes;
    return result;
}

quint64 SharedMemoryFrameRingWriter::droppedFrames() const {
    return header_ ? header_->droppedFrames : 0;
}

SharedFrameSlotHeader *SharedMemoryFrameRingWriter::slotHeaderFor(quint32 slotIndex) const {
    char *base = static_cast<char *>(mapped_) + sizeof(SharedFrameRingHeader)
        + static_cast<qsizetype>(header_->slotStride) * slotIndex;
    return reinterpret_cast<SharedFrameSlotHeader *>(base);
}

char *SharedMemoryFrameRingWriter::slotPayloadFor(quint32 slotIndex) const {
    return reinterpret_cast<char *>(slotHeaderFor(slotIndex) + 1);
}

void SharedMemoryFrameRingWriter::cleanup() {
    if (mapped_) {
        ::munmap(mapped_, mapBytes_);
        mapped_ = nullptr;
    }
    header_ = nullptr;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!shmName_.isEmpty()) {
        ::shm_unlink(shmName_.toUtf8().constData());
    }
}
