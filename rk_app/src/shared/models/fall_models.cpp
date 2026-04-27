#include "models/fall_models.h"

#include <sys/mman.h>
#include <unistd.h>

AnalysisDmaBufPayload::~AnalysisDmaBufPayload() {
    if (mapped && mapped != MAP_FAILED && mappedBytes > 0) {
        ::munmap(mapped, static_cast<size_t>(mappedBytes));
    }
    mapped = nullptr;
    mappedBytes = 0;

    if (fd >= 0) {
        ::close(fd);
    }
    fd = -1;
}

const char *AnalysisDmaBufPayload::data() const {
    if (!mapped || mapped == MAP_FAILED || offset < 0 || payloadBytes < 0 || offset + payloadBytes > mappedBytes) {
        return nullptr;
    }
    return static_cast<const char *>(mapped) + offset;
}
