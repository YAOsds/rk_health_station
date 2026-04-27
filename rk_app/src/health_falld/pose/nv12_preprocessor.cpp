#include "pose/nv12_preprocessor.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QtGlobal>

namespace {
unsigned char clampToByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<unsigned char>(value);
}

bool validateRgbFrameShape(const AnalysisFramePacket &frame, QString *error) {
    if (error) {
        error->clear();
    }

    if (frame.pixelFormat != AnalysisPixelFormat::Rgb) {
        if (error) {
            *error = QStringLiteral("pose_input_not_rgb");
        }
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_dimensions");
        }
        return false;
    }

    const int expectedBytes = frame.width * frame.height * 3;
    if (frame.payload.size() != expectedBytes) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_rgb_payload");
        }
        return false;
    }
    return true;
}

PosePreprocessResult letterboxRgbImage(
    const QImage &inputImage, int targetWidth, int targetHeight, QString *error) {
    PosePreprocessResult result;
    if (error) {
        error->clear();
    }

    if (inputImage.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_image");
        }
        return result;
    }

    QImage letterboxed(targetWidth, targetHeight, QImage::Format_RGB888);
    letterboxed.fill(QColor(114, 114, 114));

    const qreal scale = qMin(
        static_cast<qreal>(targetWidth) / inputImage.width(),
        static_cast<qreal>(targetHeight) / inputImage.height());
    const int scaledWidth = qRound(inputImage.width() * scale);
    const int scaledHeight = qRound(inputImage.height() * scale);
    const int xPad = (targetWidth - scaledWidth) / 2;
    const int yPad = (targetHeight - scaledHeight) / 2;

    {
        QPainter painter(&letterboxed);
        painter.drawImage(QRect(xPad, yPad, scaledWidth, scaledHeight), inputImage);
    }

    result.packedRgb.resize(targetWidth * targetHeight * 3);
    for (int y = 0; y < targetHeight; ++y) {
        memcpy(result.packedRgb.data() + (y * targetWidth * 3),
            letterboxed.constScanLine(y), targetWidth * 3);
    }
    result.xPad = xPad;
    result.yPad = yPad;
    result.scale = static_cast<float>(scale);
    return result;
}
}

PosePreprocessResult preprocessNv12ForPose(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error) {
    PosePreprocessResult result;
    if (error) {
        error->clear();
    }

    if (frame.pixelFormat != AnalysisPixelFormat::Nv12) {
        if (error) {
            *error = QStringLiteral("pose_input_not_nv12");
        }
        return result;
    }
    if (frame.width <= 0 || frame.height <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_dimensions");
        }
        return result;
    }

    const int expectedBytes = frame.width * frame.height * 3 / 2;
    if (frame.payload.size() != expectedBytes) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_nv12_payload");
        }
        return result;
    }

    const float scale = qMin(static_cast<float>(targetWidth) / frame.width,
        static_cast<float>(targetHeight) / frame.height);
    const int scaledWidth = qMax(1, qRound(frame.width * scale));
    const int scaledHeight = qMax(1, qRound(frame.height * scale));
    const int xPad = (targetWidth - scaledWidth) / 2;
    const int yPad = (targetHeight - scaledHeight) / 2;

    result.packedRgb.fill(static_cast<char>(114), targetWidth * targetHeight * 3);
    result.xPad = xPad;
    result.yPad = yPad;
    result.scale = scale;

    const unsigned char *yPlane =
        reinterpret_cast<const unsigned char *>(frame.payload.constData());
    const unsigned char *uvPlane = yPlane + (frame.width * frame.height);

    for (int outY = 0; outY < scaledHeight; ++outY) {
        const int srcY = qMin(frame.height - 1, (outY * frame.height) / scaledHeight);
        const int uvRow = (srcY / 2) * frame.width;
        for (int outX = 0; outX < scaledWidth; ++outX) {
            const int srcX = qMin(frame.width - 1, (outX * frame.width) / scaledWidth);
            const int yIndex = (srcY * frame.width) + srcX;
            const int uvIndex = uvRow + ((srcX / 2) * 2);

            const int y = static_cast<int>(yPlane[yIndex]);
            const int u = static_cast<int>(uvPlane[uvIndex]) - 128;
            const int v = static_cast<int>(uvPlane[uvIndex + 1]) - 128;

            const int c = y - 16;
            const int d = u;
            const int e = v;

            const int r = (298 * c + 409 * e + 128) >> 8;
            const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            const int b = (298 * c + 516 * d + 128) >> 8;

            const int destPixel = ((outY + yPad) * targetWidth) + outX + xPad;
            result.packedRgb[destPixel * 3] = static_cast<char>(clampToByte(r));
            result.packedRgb[(destPixel * 3) + 1] = static_cast<char>(clampToByte(g));
            result.packedRgb[(destPixel * 3) + 2] = static_cast<char>(clampToByte(b));
        }
    }

    return result;
}

bool canUseRgbPoseFastPath(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error) {
    if (targetWidth <= 0 || targetHeight <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_dimensions");
        }
        return false;
    }

    if (!validateRgbFrameShape(frame, error)) {
        return false;
    }

    if (error) {
        error->clear();
    }
    return frame.width == targetWidth && frame.height == targetHeight;
}

PosePreprocessResult preprocessRgbFrameForPose(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error) {
    PosePreprocessResult result;
    if (!validateRgbFrameShape(frame, error)) {
        return result;
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_dimensions");
        }
        return result;
    }

    if (frame.width == targetWidth && frame.height == targetHeight) {
        if (error) {
            error->clear();
        }
        result.packedRgb = frame.payload;
        if (frame.posePreprocessed) {
            result.xPad = frame.poseXPad;
            result.yPad = frame.poseYPad;
            result.scale = frame.poseScale;
        }
        return result;
    }

    const QImage inputImage(reinterpret_cast<const uchar *>(frame.payload.constData()),
        frame.width, frame.height, frame.width * 3, QImage::Format_RGB888);
    return letterboxRgbImage(inputImage.copy(), targetWidth, targetHeight, error);
}
