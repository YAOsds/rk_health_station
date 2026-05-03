#include "pipeline/dma_buffer_allocator.h"

#include <linux/dma-heap.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstring>

namespace {
QString errnoMessage(const char *prefix) {
    return QStringLiteral("%1_%2_%3")
        .arg(QString::fromLatin1(prefix))
        .arg(errno)
        .arg(QString::fromLocal8Bit(strerror(errno)));
}

int allocateMemFdBuffer(int bytes, QString *error) {
#ifdef SYS_memfd_create
    const int fd = static_cast<int>(::syscall(SYS_memfd_create, "rk_analysis_dmabuf", MFD_CLOEXEC));
    if (fd < 0) {
        if (error) {
            *error = errnoMessage("analysis_memfd_create_failed");
        }
        return -1;
    }
    if (::ftruncate(fd, bytes) != 0) {
        if (error) {
            *error = errnoMessage("analysis_memfd_truncate_failed");
        }
        ::close(fd);
        return -1;
    }
    if (error) {
        error->clear();
    }
    return fd;
#else
    Q_UNUSED(bytes);
    if (error) {
        *error = QStringLiteral("analysis_memfd_unsupported");
    }
    return -1;
#endif
}
}

int DmaBufferAllocator::allocate(const QString &heapPath, int bytes, QString *error) const {
    if (heapPath == QStringLiteral("memfd")) {
        return allocateMemFdBuffer(bytes, error);
    }

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

bool DmaBufferAllocator::writePayload(int fd, const QByteArray &payload, QString *error) const {
    void *mapped = ::mmap(
        nullptr, static_cast<size_t>(payload.size()), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        if (error) {
            *error = errnoMessage("analysis_dmabuf_mmap_failed");
        }
        return false;
    }
    memcpy(mapped, payload.constData(), static_cast<size_t>(payload.size()));
    ::munmap(mapped, static_cast<size_t>(payload.size()));
    if (error) {
        error->clear();
    }
    return true;
}
