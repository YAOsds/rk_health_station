#include "analysis/rga_frame_converter.h"

#ifndef RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#define RKAPP_ENABLE_RGA_ANALYSIS_CONVERT 0
#endif

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#include <rga/RgaUtils.h>
#include <rga/im2d.h>

#include <QtGlobal>
#endif

namespace {
int nv12FrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 / 2 : 0;
}

int rgbFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 : 0;
}
}

bool RgaFrameConverter::convertNv12ToRgb(const QByteArray &nv12,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    QByteArray *rgb,
    AnalysisFrameConversionMetadata *metadata,
    QString *error) {
    if (error) {
        error->clear();
    }
    if (metadata) {
        *metadata = AnalysisFrameConversionMetadata();
    }
    if (!rgb) {
        if (error) {
            *error = QStringLiteral("missing_output_buffer");
        }
        return false;
    }

    const int inputBytes = nv12FrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || nv12.size() < inputBytes) {
        if (error) {
            *error = QStringLiteral("invalid_frame_geometry");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    rgb->resize(outputBytes);
    rga_buffer_t src = wrapbuffer_virtualaddr(
        const_cast<char *>(nv12.constData()), srcWidth, srcHeight, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr(
        rgb->data(), dstWidth, dstHeight, RK_FORMAT_RGB_888);

    const float scale = qMin(static_cast<float>(dstWidth) / srcWidth,
        static_cast<float>(dstHeight) / srcHeight);
    const int scaledWidth = qMax(1, qRound(srcWidth * scale));
    const int scaledHeight = qMax(1, qRound(srcHeight * scale));
    const int xPad = (dstWidth - scaledWidth) / 2;
    const int yPad = (dstHeight - scaledHeight) / 2;

    im_rect fullRect{0, 0, dstWidth, dstHeight};
    int fillColor = 0;
    char *fillBytes = reinterpret_cast<char *>(&fillColor);
    fillBytes[0] = 114;
    fillBytes[1] = 114;
    fillBytes[2] = 114;
    fillBytes[3] = 114;
    const IM_STATUS fillStatus = imfill(dst, fullRect, fillColor);
    if (fillStatus != IM_STATUS_SUCCESS && fillStatus != IM_STATUS_NOERROR) {
        rgb->fill(static_cast<char>(114));
    }

    rga_buffer_t pat{};
    im_rect srcRect{0, 0, srcWidth, srcHeight};
    im_rect dstRect{xPad, yPad, scaledWidth, scaledHeight};
    im_rect patRect{};
    const IM_STATUS resizeStatus = improcess(src, dst, pat, srcRect, dstRect, patRect, 0);
    if (resizeStatus != IM_STATUS_SUCCESS && resizeStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        rgb->clear();
        return false;
    }
    if (metadata) {
        metadata->posePreprocessed = true;
        metadata->poseXPad = xPad;
        metadata->poseYPad = yPad;
        metadata->poseScale = scale;
    }
    return true;
#else
    Q_UNUSED(nv12);
    Q_UNUSED(srcWidth);
    Q_UNUSED(srcHeight);
    Q_UNUSED(dstWidth);
    Q_UNUSED(dstHeight);
    Q_UNUSED(metadata);
    if (error) {
        *error = QStringLiteral("rga_analysis_convert_not_built");
    }
    rgb->clear();
    return false;
#endif
}
