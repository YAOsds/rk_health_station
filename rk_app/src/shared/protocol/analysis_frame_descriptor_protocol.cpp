#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QDataStream>

namespace {
constexpr quint32 kDescriptorMagic = 0x524B4144; // RKAD
constexpr quint16 kDescriptorVersion = 3;
constexpr quint32 kMaxDescriptorSlotIndex = 64;

bool isKnownPixelFormat(qint32 value) {
    switch (static_cast<AnalysisPixelFormat>(value)) {
    case AnalysisPixelFormat::Jpeg:
    case AnalysisPixelFormat::Nv12:
    case AnalysisPixelFormat::Rgb:
        return true;
    }
    return false;
}

bool isKnownPayloadTransport(qint32 value) {
    switch (static_cast<AnalysisPayloadTransport>(value)) {
    case AnalysisPayloadTransport::SharedMemory:
    case AnalysisPayloadTransport::DmaBuf:
        return true;
    }
    return false;
}

bool hasValidDmaBufMetadata(const AnalysisFrameDescriptor &descriptor) {
    if (descriptor.payloadTransport == AnalysisPayloadTransport::SharedMemory) {
        return descriptor.dmaBufPlaneCount == 0 && descriptor.dmaBufOffset == 0
            && descriptor.dmaBufStrideBytes == 0;
    }
    if (descriptor.dmaBufPlaneCount == 0 || descriptor.dmaBufPlaneCount > 4) {
        return false;
    }
    if (descriptor.dmaBufStrideBytes == 0 || descriptor.dmaBufOffset >= descriptor.payloadBytes) {
        return false;
    }
    return true;
}

bool hasValidDescriptorShape(const AnalysisFrameDescriptor &descriptor) {
    if (descriptor.cameraId.isEmpty() || descriptor.width <= 0 || descriptor.height <= 0) {
        return false;
    }
    if (descriptor.slotIndex >= kMaxDescriptorSlotIndex || descriptor.sequence == 0
        || descriptor.payloadBytes == 0) {
        return false;
    }

    const qint64 pixelCount = static_cast<qint64>(descriptor.width) * descriptor.height;
    if (pixelCount <= 0) {
        return false;
    }

    if (!hasValidDmaBufMetadata(descriptor)) {
        return false;
    }

    switch (descriptor.pixelFormat) {
    case AnalysisPixelFormat::Rgb:
        if (descriptor.payloadTransport == AnalysisPayloadTransport::DmaBuf) {
            return descriptor.dmaBufStrideBytes >= static_cast<quint32>(descriptor.width * 3)
                && descriptor.payloadBytes >= static_cast<quint32>(descriptor.dmaBufStrideBytes * descriptor.height);
        }
        return descriptor.payloadBytes == static_cast<quint32>(pixelCount * 3);
    case AnalysisPixelFormat::Nv12:
        if (descriptor.payloadTransport == AnalysisPayloadTransport::DmaBuf) {
            return descriptor.payloadBytes >= static_cast<quint32>(pixelCount * 3 / 2);
        }
        return descriptor.payloadBytes == static_cast<quint32>(pixelCount * 3 / 2);
    case AnalysisPixelFormat::Jpeg:
        return descriptor.payloadBytes > 0;
    }
    return false;
}

QByteArray encodeAnalysisFrameDescriptorPayload(const AnalysisFrameDescriptor &descriptor) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << kDescriptorMagic
           << kDescriptorVersion
           << descriptor.frameId
           << descriptor.timestampMs
           << descriptor.cameraId
           << descriptor.width
           << descriptor.height
           << static_cast<qint32>(descriptor.pixelFormat)
           << descriptor.posePreprocessed
           << descriptor.poseXPad
           << descriptor.poseYPad
           << descriptor.poseScale
           << static_cast<qint32>(descriptor.payloadTransport)
           << descriptor.dmaBufPlaneCount
           << descriptor.dmaBufOffset
           << descriptor.dmaBufStrideBytes
           << descriptor.slotIndex
           << descriptor.sequence
           << descriptor.payloadBytes;
    return payload;
}

bool decodeAnalysisFrameDescriptorPayload(
    const QByteArray &payload, AnalysisFrameDescriptor *descriptor) {
    if (!descriptor) {
        return false;
    }

    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint16 version = 0;
    qint32 pixelFormatValue = 0;
    qint32 payloadTransportValue = static_cast<qint32>(AnalysisPayloadTransport::SharedMemory);

    stream >> magic
           >> version
           >> descriptor->frameId
           >> descriptor->timestampMs
           >> descriptor->cameraId
           >> descriptor->width
           >> descriptor->height
           >> pixelFormatValue;

    if (stream.status() != QDataStream::Ok || magic != kDescriptorMagic
        || (version < 1 || version > kDescriptorVersion)) {
        return false;
    }

    descriptor->posePreprocessed = false;
    descriptor->poseXPad = 0;
    descriptor->poseYPad = 0;
    descriptor->poseScale = 1.0f;
    descriptor->payloadTransport = AnalysisPayloadTransport::SharedMemory;
    descriptor->dmaBufPlaneCount = 0;
    descriptor->dmaBufOffset = 0;
    descriptor->dmaBufStrideBytes = 0;
    if (version >= 2) {
        stream >> descriptor->posePreprocessed
               >> descriptor->poseXPad
               >> descriptor->poseYPad
               >> descriptor->poseScale;
    }
    if (version >= 3) {
        stream >> payloadTransportValue
               >> descriptor->dmaBufPlaneCount
               >> descriptor->dmaBufOffset
               >> descriptor->dmaBufStrideBytes;
    }

    stream >> descriptor->slotIndex
           >> descriptor->sequence
           >> descriptor->payloadBytes;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    if (!isKnownPixelFormat(pixelFormatValue) || !isKnownPayloadTransport(payloadTransportValue)) {
        return false;
    }

    descriptor->pixelFormat = static_cast<AnalysisPixelFormat>(pixelFormatValue);
    descriptor->payloadTransport = static_cast<AnalysisPayloadTransport>(payloadTransportValue);
    return hasValidDescriptorShape(*descriptor);
}
} // namespace

QByteArray encodeAnalysisFrameDescriptor(const AnalysisFrameDescriptor &descriptor) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    const QByteArray payload = encodeAnalysisFrameDescriptorPayload(descriptor);
    stream << static_cast<quint32>(payload.size());
    bytes.append(payload);
    return bytes;
}

bool decodeAnalysisFrameDescriptor(const QByteArray &bytes, AnalysisFrameDescriptor *descriptor) {
    QByteArray copy = bytes;
    if (!takeFirstAnalysisFrameDescriptor(&copy, descriptor)) {
        return false;
    }
    return copy.isEmpty();
}

bool takeFirstAnalysisFrameDescriptor(QByteArray *bytes, AnalysisFrameDescriptor *descriptor) {
    if (!bytes || !descriptor) {
        return false;
    }
    if (bytes->size() < static_cast<int>(sizeof(quint32))) {
        return false;
    }

    QByteArray lengthBytes = bytes->left(static_cast<int>(sizeof(quint32)));
    QDataStream lengthStream(&lengthBytes, QIODevice::ReadOnly);
    lengthStream.setVersion(QDataStream::Qt_5_15);

    quint32 payloadSize = 0;
    lengthStream >> payloadSize;

    const int totalSize = static_cast<int>(sizeof(quint32) + payloadSize);
    if (bytes->size() < totalSize) {
        return false;
    }

    const QByteArray payload = bytes->mid(static_cast<int>(sizeof(quint32)), payloadSize);
    if (!decodeAnalysisFrameDescriptorPayload(payload, descriptor)) {
        return false;
    }

    bytes->remove(0, totalSize);
    return true;
}
