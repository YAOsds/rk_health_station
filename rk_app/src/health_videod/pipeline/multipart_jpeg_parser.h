#pragma once

#include <QByteArray>

class MultipartJpegParser {
public:
    bool takeFrame(QByteArray *streamBuffer, const QByteArray &boundaryMarker, QByteArray *jpegBytes) const;
};
