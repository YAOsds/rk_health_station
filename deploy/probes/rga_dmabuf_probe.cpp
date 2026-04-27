#include <cstddef>

#include <rga/RgaUtils.h>
#include <rga/im2d.h>

#include <linux/dma-heap.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr int kChannels = 3;
constexpr int kBufferBytes = kWidth * kHeight * kChannels;

int allocateDmaBuffer(const char *heapPath, int bytes) {
    const int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        std::fprintf(stderr, "open_heap_failed path=%s errno=%d %s\n", heapPath, errno, strerror(errno));
        return -1;
    }

    dma_heap_allocation_data allocation{};
    allocation.len = static_cast<__u64>(bytes);
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    allocation.heap_flags = 0;
    if (ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocation) != 0) {
        std::fprintf(stderr, "dma_heap_alloc_failed path=%s errno=%d %s\n", heapPath, errno, strerror(errno));
        close(heapFd);
        return -1;
    }

    close(heapFd);
    return static_cast<int>(allocation.fd);
}

void fillPattern(uint8_t *data, int bytes) {
    for (int index = 0; index < bytes; ++index) {
        data[index] = static_cast<uint8_t>((index * 17 + 29) & 0xff);
    }
}

int compareBuffers(const uint8_t *left, const uint8_t *right, int bytes) {
    for (int index = 0; index < bytes; ++index) {
        if (left[index] != right[index]) {
            return index;
        }
    }
    return -1;
}
}

int main(int argc, char **argv) {
    const char *heapPath = argc > 1 ? argv[1] : "/dev/dma_heap/system-uncached-dma32";
    const int srcFd = allocateDmaBuffer(heapPath, kBufferBytes);
    if (srcFd < 0) {
        return 2;
    }
    const int dstFd = allocateDmaBuffer(heapPath, kBufferBytes);
    if (dstFd < 0) {
        close(srcFd);
        return 2;
    }

    auto *srcMap = static_cast<uint8_t *>(mmap(nullptr, kBufferBytes, PROT_READ | PROT_WRITE, MAP_SHARED, srcFd, 0));
    auto *dstMap = static_cast<uint8_t *>(mmap(nullptr, kBufferBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dstFd, 0));
    if (srcMap == MAP_FAILED || dstMap == MAP_FAILED) {
        std::fprintf(stderr, "mmap_failed errno=%d %s\n", errno, strerror(errno));
        close(srcFd);
        close(dstFd);
        return 3;
    }

    fillPattern(srcMap, kBufferBytes);
    std::memset(dstMap, 0, kBufferBytes);

    rga_buffer_handle_t srcHandle = importbuffer_fd(srcFd, kBufferBytes);
    rga_buffer_handle_t dstHandle = importbuffer_fd(dstFd, kBufferBytes);
    if (!srcHandle || !dstHandle) {
        std::fprintf(stderr, "rga_importbuffer_fd_failed src_handle=%u dst_handle=%u\n", srcHandle, dstHandle);
        if (srcHandle) releasebuffer_handle(srcHandle);
        if (dstHandle) releasebuffer_handle(dstHandle);
        munmap(srcMap, kBufferBytes);
        munmap(dstMap, kBufferBytes);
        close(srcFd);
        close(dstFd);
        return 4;
    }

    rga_buffer_t src = wrapbuffer_handle(srcHandle, kWidth, kHeight, RK_FORMAT_RGB_888);
    rga_buffer_t dst = wrapbuffer_handle(dstHandle, kWidth, kHeight, RK_FORMAT_RGB_888);

    IM_STATUS status = imcheck(src, dst, {}, {});
    if (status != IM_STATUS_NOERROR) {
        std::fprintf(stderr, "rga_imcheck_failed status=%d %s\n", status, imStrError(status));
        releasebuffer_handle(srcHandle);
        releasebuffer_handle(dstHandle);
        munmap(srcMap, kBufferBytes);
        munmap(dstMap, kBufferBytes);
        close(srcFd);
        close(dstFd);
        return 5;
    }

    status = imcopy(src, dst);
    if (status != IM_STATUS_SUCCESS) {
        std::fprintf(stderr, "rga_imcopy_failed status=%d %s\n", status, imStrError(status));
        releasebuffer_handle(srcHandle);
        releasebuffer_handle(dstHandle);
        munmap(srcMap, kBufferBytes);
        munmap(dstMap, kBufferBytes);
        close(srcFd);
        close(dstFd);
        return 6;
    }

    const int mismatch = compareBuffers(srcMap, dstMap, kBufferBytes);
    releasebuffer_handle(srcHandle);
    releasebuffer_handle(dstHandle);
    munmap(srcMap, kBufferBytes);
    munmap(dstMap, kBufferBytes);
    close(srcFd);
    close(dstFd);

    if (mismatch >= 0) {
        std::fprintf(stderr, "verify_failed mismatch_index=%d\n", mismatch);
        return 7;
    }

    std::printf("rga_dmabuf_probe_ok heap=%s bytes=%d width=%d height=%d format=RGB888\n",
        heapPath, kBufferBytes, kWidth, kHeight);
    return 0;
}
