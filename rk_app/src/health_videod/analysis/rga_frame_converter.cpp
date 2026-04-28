#include "analysis/rga_frame_converter.h"

#ifndef RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#define RKAPP_ENABLE_RGA_ANALYSIS_CONVERT 0
#endif

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
#include <rga/RgaUtils.h>
#include <rga/im2d.h>

#include <QByteArray>
#include <QtGlobal>
#include <linux/dma-heap.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace {
int nv12FrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 / 2 : 0;
}

int rgbFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 : 0;
}

int uyvyFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 2 : 0;
}

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
const char kAnalysisDmaHeapEnvVar[] = "RK_VIDEO_ANALYSIS_DMA_HEAP";
const char kDefaultAnalysisDmaHeap[] = "/dev/dma_heap/system-uncached-dma32";

QString errnoMessage(const char *prefix) {
    return QStringLiteral("%1_%2_%3")
        .arg(QString::fromLatin1(prefix))
        .arg(errno)
        .arg(QString::fromLocal8Bit(strerror(errno)));
}

QString analysisDmaHeapPath() {
    const QString heap = qEnvironmentVariable(kAnalysisDmaHeapEnvVar).trimmed();
    return heap.isEmpty() ? QString::fromLatin1(kDefaultAnalysisDmaHeap) : heap;
}

// The C++ wrapbuffer_handle overload takes (handle, width, height, format, wstride, hstride),
// which differs from the C macro ordering documented above it.
rga_buffer_t wrapHandleBufferWithStride(
    rga_buffer_handle_t handle, int width, int height, int format, int wstride, int hstride) {
    return wrapbuffer_handle(handle, width, height, format, wstride, hstride);
}

int allocateDmaHeapBuffer(const QString &heapPath, int bytes, QString *error) {
    const int heapFd = ::open(heapPath.toUtf8().constData(), O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        if (error) {
            *error = errnoMessage("analysis_dma_heap_open_failed");
        }
        return -1;
    }

    dma_heap_allocation_data allocation{};
    allocation.len = static_cast<__u64>(bytes);
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    allocation.heap_flags = 0;
    if (::ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocation) != 0) {
        if (error) {
            *error = errnoMessage("analysis_dma_heap_alloc_failed");
        }
        ::close(heapFd);
        return -1;
    }

    ::close(heapFd);
    if (error) {
        error->clear();
    }
    return static_cast<int>(allocation.fd);
}

bool fillDmaBufferFallback(int fd, int bytes, char value) {
    void *mapped = ::mmap(nullptr, static_cast<size_t>(bytes), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        return false;
    }
    memset(mapped, value, static_cast<size_t>(bytes));
    ::munmap(mapped, static_cast<size_t>(bytes));
    return true;
}
#endif
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


bool RgaFrameConverter::convertNv12ToRgbDma(const QByteArray &nv12,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    AnalysisDmaBuffer *rgb,
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
    *rgb = AnalysisDmaBuffer();

    const int inputBytes = nv12FrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || nv12.size() < inputBytes) {
        if (error) {
            *error = QStringLiteral("invalid_frame_geometry");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    QString dmaError;
    const int outputFd = allocateDmaHeapBuffer(analysisDmaHeapPath(), outputBytes, &dmaError);
    if (outputFd < 0) {
        if (error) {
            *error = dmaError.isEmpty() ? QStringLiteral("analysis_dma_heap_alloc_failed") : dmaError;
        }
        return false;
    }

    rga_buffer_handle_t inputHandle = importbuffer_virtualaddr(
        const_cast<char *>(nv12.constData()), inputBytes);
    if (!inputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_input_virtual_failed");
        }
        ::close(outputFd);
        return false;
    }

    rga_buffer_handle_t outputHandle = importbuffer_fd(outputFd, outputBytes);
    if (!outputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_output_fd_failed");
        }
        releasebuffer_handle(inputHandle);
        ::close(outputFd);
        return false;
    }

    rga_buffer_t src = wrapbuffer_handle(inputHandle, srcWidth, srcHeight, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_handle(outputHandle, dstWidth, dstHeight, RK_FORMAT_RGB_888);

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
        fillDmaBufferFallback(outputFd, outputBytes, static_cast<char>(114));
    }

    rga_buffer_t pat{};
    im_rect srcRect{0, 0, srcWidth, srcHeight};
    im_rect dstRect{xPad, yPad, scaledWidth, scaledHeight};
    im_rect patRect{};
    const IM_STATUS resizeStatus = improcess(src, dst, pat, srcRect, dstRect, patRect, 0);
    releasebuffer_handle(inputHandle);
    releasebuffer_handle(outputHandle);
    if (resizeStatus != IM_STATUS_SUCCESS && resizeStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        ::close(outputFd);
        return false;
    }

    if (metadata) {
        metadata->posePreprocessed = true;
        metadata->poseXPad = xPad;
        metadata->poseYPad = yPad;
        metadata->poseScale = scale;
    }
    rgb->fd = outputFd;
    rgb->payloadBytes = static_cast<quint32>(outputBytes);
    rgb->offset = 0;
    rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
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
    return false;
#endif
}


bool RgaFrameConverter::convertNv12DmaToRgbDma(const AnalysisDmaBuffer &nv12,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    AnalysisDmaBuffer *rgb,
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
    *rgb = AnalysisDmaBuffer();

    const int inputBytes = nv12FrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || nv12.fd < 0 || nv12.payloadBytes < static_cast<quint32>(inputBytes)) {
        if (error) {
            *error = QStringLiteral("invalid_dma_frame_geometry");
        }
        return false;
    }
    if (nv12.offset != 0) {
        if (error) {
            *error = QStringLiteral("rga_dma_input_offset_unsupported");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    QString dmaError;
    const int outputFd = allocateDmaHeapBuffer(analysisDmaHeapPath(), outputBytes, &dmaError);
    if (outputFd < 0) {
        if (error) {
            *error = dmaError.isEmpty() ? QStringLiteral("analysis_dma_heap_alloc_failed") : dmaError;
        }
        return false;
    }

    rga_buffer_handle_t inputHandle = importbuffer_fd(nv12.fd, static_cast<int>(nv12.payloadBytes));
    if (!inputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_input_fd_failed");
        }
        ::close(outputFd);
        return false;
    }

    rga_buffer_handle_t outputHandle = importbuffer_fd(outputFd, outputBytes);
    if (!outputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_output_fd_failed");
        }
        releasebuffer_handle(inputHandle);
        ::close(outputFd);
        return false;
    }

    const int srcStride = nv12.strideBytes > 0 ? static_cast<int>(nv12.strideBytes) : srcWidth;
    rga_buffer_t src = wrapHandleBufferWithStride(
        inputHandle, srcWidth, srcHeight, RK_FORMAT_YCbCr_420_SP, srcStride, srcHeight);
    rga_buffer_t dst = wrapbuffer_handle(outputHandle, dstWidth, dstHeight, RK_FORMAT_RGB_888);

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
        fillDmaBufferFallback(outputFd, outputBytes, static_cast<char>(114));
    }

    rga_buffer_t pat{};
    im_rect srcRect{0, 0, srcWidth, srcHeight};
    im_rect dstRect{xPad, yPad, scaledWidth, scaledHeight};
    im_rect patRect{};
    const IM_STATUS resizeStatus = improcess(src, dst, pat, srcRect, dstRect, patRect, 0);
    releasebuffer_handle(inputHandle);
    releasebuffer_handle(outputHandle);
    if (resizeStatus != IM_STATUS_SUCCESS && resizeStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        ::close(outputFd);
        return false;
    }

    if (metadata) {
        metadata->posePreprocessed = true;
        metadata->poseXPad = xPad;
        metadata->poseYPad = yPad;
        metadata->poseScale = scale;
    }
    rgb->fd = outputFd;
    rgb->payloadBytes = static_cast<quint32>(outputBytes);
    rgb->offset = 0;
    rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
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
    return false;
#endif
}


bool RgaFrameConverter::convertUyvyToRgb(const QByteArray &uyvy,
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

    const int inputBytes = uyvyFrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || uyvy.size() < inputBytes) {
        if (error) {
            *error = QStringLiteral("invalid_uyvy_frame_geometry");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    rgb->resize(outputBytes);
    rga_buffer_t src = wrapbuffer_virtualaddr(
        const_cast<char *>(uyvy.constData()), srcWidth, srcHeight, RK_FORMAT_UYVY_422);
    rga_buffer_t dst = wrapbuffer_virtualaddr(rgb->data(), dstWidth, dstHeight, RK_FORMAT_RGB_888);

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
    Q_UNUSED(uyvy);
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

bool RgaFrameConverter::convertUyvyToRgbDma(const QByteArray &uyvy,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    AnalysisDmaBuffer *rgb,
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
    *rgb = AnalysisDmaBuffer();

    const int inputBytes = uyvyFrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || uyvy.size() < inputBytes) {
        if (error) {
            *error = QStringLiteral("invalid_uyvy_frame_geometry");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    QString dmaError;
    const int outputFd = allocateDmaHeapBuffer(analysisDmaHeapPath(), outputBytes, &dmaError);
    if (outputFd < 0) {
        if (error) {
            *error = dmaError.isEmpty() ? QStringLiteral("analysis_dma_heap_alloc_failed") : dmaError;
        }
        return false;
    }

    rga_buffer_t src = wrapbuffer_virtualaddr(
        const_cast<char *>(uyvy.constData()), srcWidth, srcHeight, RK_FORMAT_UYVY_422);
    rga_buffer_handle_t outputHandle = importbuffer_fd(outputFd, outputBytes);
    if (!outputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_output_fd_failed");
        }
        ::close(outputFd);
        return false;
    }
    rga_buffer_t dst = wrapbuffer_handle(outputHandle, dstWidth, dstHeight, RK_FORMAT_RGB_888);

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
        fillDmaBufferFallback(outputFd, outputBytes, static_cast<char>(114));
    }

    rga_buffer_t pat{};
    im_rect srcRect{0, 0, srcWidth, srcHeight};
    im_rect dstRect{xPad, yPad, scaledWidth, scaledHeight};
    im_rect patRect{};
    const IM_STATUS resizeStatus = improcess(src, dst, pat, srcRect, dstRect, patRect, 0);
    releasebuffer_handle(outputHandle);
    if (resizeStatus != IM_STATUS_SUCCESS && resizeStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        ::close(outputFd);
        return false;
    }

    if (metadata) {
        metadata->posePreprocessed = true;
        metadata->poseXPad = xPad;
        metadata->poseYPad = yPad;
        metadata->poseScale = scale;
    }
    rgb->fd = outputFd;
    rgb->payloadBytes = static_cast<quint32>(outputBytes);
    rgb->offset = 0;
    rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
    return true;
#else
    Q_UNUSED(uyvy);
    Q_UNUSED(srcWidth);
    Q_UNUSED(srcHeight);
    Q_UNUSED(dstWidth);
    Q_UNUSED(dstHeight);
    Q_UNUSED(metadata);
    if (error) {
        *error = QStringLiteral("rga_analysis_convert_not_built");
    }
    return false;
#endif
}

bool RgaFrameConverter::convertUyvyDmaToRgbDma(const AnalysisDmaBuffer &uyvy,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    AnalysisDmaBuffer *rgb,
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
    *rgb = AnalysisDmaBuffer();

    const int inputBytes = uyvyFrameBytes(srcWidth, srcHeight);
    const int outputBytes = rgbFrameBytes(dstWidth, dstHeight);
    if (inputBytes <= 0 || outputBytes <= 0 || uyvy.fd < 0
        || uyvy.payloadBytes < static_cast<quint32>(inputBytes)) {
        if (error) {
            *error = QStringLiteral("invalid_uyvy_dma_frame_geometry");
        }
        return false;
    }
    if (uyvy.offset != 0) {
        if (error) {
            *error = QStringLiteral("rga_dma_input_offset_unsupported");
        }
        return false;
    }

#if RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    QString dmaError;
    const int outputFd = allocateDmaHeapBuffer(analysisDmaHeapPath(), outputBytes, &dmaError);
    if (outputFd < 0) {
        if (error) {
            *error = dmaError.isEmpty() ? QStringLiteral("analysis_dma_heap_alloc_failed") : dmaError;
        }
        return false;
    }

    rga_buffer_handle_t inputHandle = importbuffer_fd(uyvy.fd, static_cast<int>(uyvy.payloadBytes));
    if (!inputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_input_fd_failed");
        }
        ::close(outputFd);
        return false;
    }

    rga_buffer_handle_t outputHandle = importbuffer_fd(outputFd, outputBytes);
    if (!outputHandle) {
        if (error) {
            *error = QStringLiteral("rga_import_output_fd_failed");
        }
        releasebuffer_handle(inputHandle);
        ::close(outputFd);
        return false;
    }

    const int srcStride = uyvy.strideBytes > 0 ? static_cast<int>(uyvy.strideBytes) : srcWidth * 2;
    rga_buffer_t src = wrapHandleBufferWithStride(
        inputHandle, srcWidth, srcHeight, RK_FORMAT_UYVY_422, srcStride / 2, srcHeight);
    rga_buffer_t dst = wrapbuffer_handle(outputHandle, dstWidth, dstHeight, RK_FORMAT_RGB_888);

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
        fillDmaBufferFallback(outputFd, outputBytes, static_cast<char>(114));
    }

    rga_buffer_t pat{};
    im_rect srcRect{0, 0, srcWidth, srcHeight};
    im_rect dstRect{xPad, yPad, scaledWidth, scaledHeight};
    im_rect patRect{};
    const IM_STATUS resizeStatus = improcess(src, dst, pat, srcRect, dstRect, patRect, 0);
    releasebuffer_handle(inputHandle);
    releasebuffer_handle(outputHandle);
    if (resizeStatus != IM_STATUS_SUCCESS && resizeStatus != IM_STATUS_NOERROR) {
        if (error) {
            *error = QString::fromLatin1(imStrError(resizeStatus));
        }
        ::close(outputFd);
        return false;
    }

    if (metadata) {
        metadata->posePreprocessed = true;
        metadata->poseXPad = xPad;
        metadata->poseYPad = yPad;
        metadata->poseScale = scale;
    }
    rgb->fd = outputFd;
    rgb->payloadBytes = static_cast<quint32>(outputBytes);
    rgb->offset = 0;
    rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
    return true;
#else
    Q_UNUSED(uyvy);
    Q_UNUSED(srcWidth);
    Q_UNUSED(srcHeight);
    Q_UNUSED(dstWidth);
    Q_UNUSED(dstHeight);
    Q_UNUSED(metadata);
    if (error) {
        *error = QStringLiteral("rga_analysis_convert_not_built");
    }
    return false;
#endif
}
