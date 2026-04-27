#include <linux/dma-heap.h>
#include <rknn/rknn_api.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const char *kDefaultHeapPath = "/dev/dma_heap/system-uncached-dma32";
const char *kDefaultModelPath = "/home/elf/rk3588_bundle/assets/models/yolov8n-pose.rknn";

int64_t nowNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

uint32_t tensorBytes(const rknn_tensor_attr &attr) {
    return attr.size_with_stride > 0 ? attr.size_with_stride : attr.size;
}

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

void dumpAttr(const char *prefix, const rknn_tensor_attr &attr) {
    std::printf("%s index=%u n_dims=%u dims=[", prefix, attr.index, attr.n_dims);
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        std::printf("%s%u", i == 0 ? "" : ",", attr.dims[i]);
    }
    std::printf("] size=%u size_with_stride=%u fmt=%d type=%d qnt=%d\n",
        attr.size, attr.size_with_stride, attr.fmt, attr.type, attr.qnt_type);
}
}

int main(int argc, char **argv) {
    const char *modelPath = argc > 1 ? argv[1] : kDefaultModelPath;
    const char *heapPath = argc > 2 ? argv[2] : kDefaultHeapPath;

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, const_cast<char *>(modelPath), 0, 0, nullptr);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "rknn_init_failed ret=%d model=%s\n", ret, modelPath);
        return 2;
    }

    rknn_input_output_num ioNum{};
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &ioNum, sizeof(ioNum));
    if (ret != RKNN_SUCC || ioNum.n_input < 1 || ioNum.n_output < 1) {
        std::fprintf(stderr, "rknn_query_io_failed ret=%d inputs=%u outputs=%u\n", ret, ioNum.n_input, ioNum.n_output);
        rknn_destroy(ctx);
        return 3;
    }

    rknn_tensor_attr inputAttr{};
    inputAttr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &inputAttr, sizeof(inputAttr));
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "rknn_query_input_attr_failed ret=%d\n", ret);
        rknn_destroy(ctx);
        return 4;
    }
    dumpAttr("input", inputAttr);

    std::vector<rknn_tensor_attr> outputAttrs(ioNum.n_output);
    bool isQuant = false;
    for (uint32_t i = 0; i < ioNum.n_output; ++i) {
        outputAttrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &outputAttrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "rknn_query_output_attr_failed index=%u ret=%d\n", i, ret);
            rknn_destroy(ctx);
            return 5;
        }
        dumpAttr("output", outputAttrs[i]);
    }
    isQuant = outputAttrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC
        && outputAttrs[0].type != RKNN_TENSOR_FLOAT16;

    rknn_tensor_attr inputIoAttr = inputAttr;
    inputIoAttr.type = RKNN_TENSOR_UINT8;
    inputIoAttr.fmt = RKNN_TENSOR_NHWC;
    inputIoAttr.pass_through = 0;
    const int inputBytes = static_cast<int>(tensorBytes(inputIoAttr));
    const int inputFd = allocateDmaBuffer(heapPath, inputBytes);
    if (inputFd < 0) {
        rknn_destroy(ctx);
        return 6;
    }

    void *inputMap = mmap(nullptr, inputBytes, PROT_READ | PROT_WRITE, MAP_SHARED, inputFd, 0);
    if (inputMap == MAP_FAILED) {
        std::fprintf(stderr, "mmap_input_failed errno=%d %s\n", errno, strerror(errno));
        close(inputFd);
        rknn_destroy(ctx);
        return 7;
    }
    std::memset(inputMap, 114, inputBytes);

    rknn_tensor_mem *inputMem = rknn_create_mem_from_fd(ctx, inputFd, inputMap, inputBytes, 0);
    if (inputMem == nullptr) {
        std::fprintf(stderr, "rknn_create_mem_from_fd_failed\n");
        munmap(inputMap, inputBytes);
        close(inputFd);
        rknn_destroy(ctx);
        return 8;
    }

    ret = rknn_set_io_mem(ctx, inputMem, &inputIoAttr);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "rknn_set_input_io_mem_failed ret=%d\n", ret);
        rknn_destroy_mem(ctx, inputMem);
        munmap(inputMap, inputBytes);
        close(inputFd);
        rknn_destroy(ctx);
        return 9;
    }

    constexpr int kRuns = 30;
    double totalRunMs = 0.0;
    double totalOutputsMs = 0.0;
    for (int run = 0; run < kRuns; ++run) {
        ret = rknn_mem_sync(ctx, inputMem, RKNN_MEMORY_SYNC_TO_DEVICE);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "rknn_mem_sync_input_failed ret=%d run=%d\n", ret, run);
            break;
        }

        const int64_t runStart = nowNs();
        ret = rknn_run(ctx, nullptr);
        const int64_t runEnd = nowNs();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "rknn_run_failed ret=%d run=%d\n", ret, run);
            break;
        }

        std::vector<rknn_output> outputs(ioNum.n_output);
        for (uint32_t i = 0; i < ioNum.n_output; ++i) {
            outputs[i].index = i;
            outputs[i].want_float = !isQuant;
        }
        const int64_t outputsStart = nowNs();
        ret = rknn_outputs_get(ctx, ioNum.n_output, outputs.data(), nullptr);
        const int64_t outputsEnd = nowNs();
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "rknn_outputs_get_failed ret=%d run=%d\n", ret, run);
            break;
        }
        rknn_outputs_release(ctx, ioNum.n_output, outputs.data());

        totalRunMs += static_cast<double>(runEnd - runStart) / 1000000.0;
        totalOutputsMs += static_cast<double>(outputsEnd - outputsStart) / 1000000.0;
    }

    rknn_destroy_mem(ctx, inputMem);
    munmap(inputMap, inputBytes);
    close(inputFd);
    rknn_destroy(ctx);

    if (ret != RKNN_SUCC) {
        return 10;
    }

    std::printf("rknn_dmabuf_input_probe_ok model=%s heap=%s runs=%d avg_run_ms=%.3f avg_outputs_get_ms=%.3f input_bytes=%d\n",
        modelPath, heapPath, kRuns, totalRunMs / kRuns, totalOutputsMs / kRuns, inputBytes);
    return 0;
}
