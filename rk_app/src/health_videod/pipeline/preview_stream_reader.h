#pragma once

#include "pipeline/multipart_jpeg_parser.h"

#include <QByteArray>
#include <QString>

class PreviewStreamReader {
public:
    struct PreviewStreamConfig {
        QString host;
        quint16 port = 0;
        QString boundary;
    };

    bool parsePreviewUrl(const QString &previewUrl, PreviewStreamConfig *config, QString *error) const;
    bool readJpegFrame(const QString &previewUrl, QByteArray *jpegBytes, QString *error) const;

private:
    MultipartJpegParser parser_;
};
