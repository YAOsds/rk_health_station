#include "analysis/rga_frame_converter.h"

#ifndef RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#define RKAPP_ENABLE_RGA_ANALYSIS_CONVERT 0
#endif

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#include <rga/RgaUtils.h>
#include <rga/im2d.h>
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
    QString *error) {
    if (error) {
        error->clear();
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

    const IM_STATUS checkStatus = imcheck(src, dst, {}, {});
    if (checkStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(checkStatus));
        }
        rgb->clear();
        return false;
    }

    const IM_STATUS resizeStatus = imresize(src, dst);
    if (resizeStatus != IM_STATUS_SUCCESS) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        rgb->clear();
        return false;
    }
    return true;
#else
    Q_UNUSED(nv12);
    Q_UNUSED(srcWidth);
    Q_UNUSED(srcHeight);
    Q_UNUSED(dstWidth);
    Q_UNUSED(dstHeight);
    if (error) {
        *error = QStringLiteral("rga_analysis_convert_not_built");
    }
    rgb->clear();
    return false;
#endif
}
