#include "ingest/dmabuf_frame_reader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
void setError(QString *error, const QString &message) {
    if (error) {
        *error = message;
    }
}

bool validateDescriptor(const AnalysisFrameDescriptor &descriptor, QString *error) {
    if (descriptor.payloadTransport != AnalysisPayloadTransport::DmaBuf) {
        setError(error, QStringLiteral("analysis_payload_transport_not_dmabuf"));
        return false;
    }
    if (descriptor.payloadBytes == 0 || descriptor.dmaBufPlaneCount == 0
        || descriptor.dmaBufStrideBytes == 0) {
        setError(error, QStringLiteral("analysis_dmabuf_descriptor_invalid"));
        return false;
    }
    if (descriptor.dmaBufOffset > descriptor.payloadBytes) {
        setError(error, QStringLiteral("analysis_dmabuf_offset_invalid"));
        return false;
    }
    return true;
}
}

bool DmaBufFrameReader::read(
    const AnalysisFrameDescriptor &descriptor, int fd, AnalysisFramePacket *packet, QString *error) {
    setError(error, QString());
    if (!packet) {
        setError(error, QStringLiteral("analysis_packet_null"));
        return false;
    }
    if (!validateDescriptor(descriptor, error)) {
        return false;
    }
    if (fd < 0) {
        setError(error, QStringLiteral("analysis_dmabuf_fd_invalid"));
        return false;
    }

    const qsizetype mapBytes = static_cast<qsizetype>(descriptor.dmaBufOffset + descriptor.payloadBytes);
    void *mapped = ::mmap(nullptr, static_cast<size_t>(mapBytes), PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        setError(error, QStringLiteral("analysis_dmabuf_mmap_failed"));
        return false;
    }

    const int ownedFd = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (ownedFd < 0) {
        ::munmap(mapped, static_cast<size_t>(mapBytes));
        setError(error, QStringLiteral("analysis_dmabuf_dup_failed"));
        return false;
    }

    auto payload = AnalysisDmaBufPayloadPtr::create();
    payload->fd = ownedFd;
    payload->mapped = mapped;
    payload->mappedBytes = mapBytes;
    payload->offset = static_cast<qsizetype>(descriptor.dmaBufOffset);
    payload->payloadBytes = static_cast<qsizetype>(descriptor.payloadBytes);
    const char *payloadData = payload->data();
    if (!payloadData) {
        setError(error, QStringLiteral("analysis_dmabuf_payload_invalid"));
        return false;
    }

    packet->frameId = descriptor.frameId;
    packet->timestampMs = descriptor.timestampMs;
    packet->cameraId = descriptor.cameraId;
    packet->width = descriptor.width;
    packet->height = descriptor.height;
    packet->pixelFormat = descriptor.pixelFormat;
    packet->posePreprocessed = descriptor.posePreprocessed;
    packet->poseXPad = descriptor.poseXPad;
    packet->poseYPad = descriptor.poseYPad;
    packet->poseScale = descriptor.poseScale;
    packet->payloadTransport = AnalysisPayloadTransport::DmaBuf;
    packet->dmaBufPlaneCount = descriptor.dmaBufPlaneCount;
    packet->dmaBufOffset = descriptor.dmaBufOffset;
    packet->dmaBufStrideBytes = descriptor.dmaBufStrideBytes;
    packet->dmaBufPayload = payload;
    packet->payload = QByteArray::fromRawData(payloadData, static_cast<int>(descriptor.payloadBytes));
    return true;
}
