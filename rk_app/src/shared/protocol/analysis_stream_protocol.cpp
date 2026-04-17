#include "protocol/analysis_stream_protocol.h"

#include <QDataStream>

namespace {
constexpr quint32 kAnalysisFrameMagic = 0x524B4146; // RKAF
constexpr quint16 kAnalysisFrameVersion = 1;

QByteArray encodeAnalysisFramePayload(const AnalysisFramePacket &packet) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << kAnalysisFrameMagic
           << kAnalysisFrameVersion
           << packet.frameId
           << packet.timestampMs
           << packet.cameraId
           << packet.width
           << packet.height
           << static_cast<qint32>(packet.pixelFormat)
           << packet.payload;
    return payload;
}

bool decodeAnalysisFramePayload(const QByteArray &payload, AnalysisFramePacket *packet) {
    if (!packet) {
        return false;
    }

    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint16 version = 0;
    qint32 pixelFormatValue = 0;

    stream >> magic
           >> version
           >> packet->frameId
           >> packet->timestampMs
           >> packet->cameraId
           >> packet->width
           >> packet->height
           >> pixelFormatValue
           >> packet->payload;

    if (stream.status() != QDataStream::Ok || magic != kAnalysisFrameMagic
        || version != kAnalysisFrameVersion) {
        return false;
    }

    packet->pixelFormat = static_cast<AnalysisPixelFormat>(pixelFormatValue);
    return true;
}
}

QByteArray encodeAnalysisFramePacket(const AnalysisFramePacket &packet) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    const QByteArray payload = encodeAnalysisFramePayload(packet);
    stream << static_cast<quint32>(payload.size());
    bytes.append(payload);
    return bytes;
}

bool decodeAnalysisFramePacket(const QByteArray &bytes, AnalysisFramePacket *packet) {
    QByteArray copy = bytes;
    if (!takeFirstAnalysisFramePacket(&copy, packet)) {
        return false;
    }
    return copy.isEmpty();
}

bool takeFirstAnalysisFramePacket(QByteArray *bytes, AnalysisFramePacket *packet) {
    if (!bytes || !packet) {
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
    if (!decodeAnalysisFramePayload(payload, packet)) {
        return false;
    }

    bytes->remove(0, totalSize);
    if (!packet) {
        return false;
    }
    return true;
}
