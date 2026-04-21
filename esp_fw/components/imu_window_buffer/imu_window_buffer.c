#include "imu_window_buffer.h"

#include <string.h>

void imu_window_buffer_init(imu_window_buffer_t *buffer, size_t window_size, size_t stride)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->window_size = window_size;
    buffer->stride = stride;
}

bool imu_window_buffer_push(imu_window_buffer_t *buffer, const imu_sample6_t *sample)
{
    memcpy(buffer->samples[buffer->write_index], sample->values, sizeof(sample->values));
    buffer->write_index = (buffer->write_index + 1U) % buffer->window_size;
    if (buffer->count < buffer->window_size) {
        buffer->count++;
    }
    buffer->since_emit++;
    if (buffer->count < buffer->window_size) {
        return false;
    }
    if (buffer->since_emit < buffer->stride) {
        return false;
    }
    buffer->since_emit = 0;
    return true;
}

void imu_window_buffer_copy_latest(const imu_window_buffer_t *buffer, float out[256][6])
{
    for (size_t i = 0; i < buffer->window_size; ++i) {
        size_t src = (buffer->write_index + i) % buffer->window_size;
        memcpy(out[i], buffer->samples[src], sizeof(buffer->samples[src]));
    }
}
