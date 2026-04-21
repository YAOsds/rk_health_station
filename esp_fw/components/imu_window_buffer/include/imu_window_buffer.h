#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float values[6];
} imu_sample6_t;

typedef struct {
    float samples[256][6];
    size_t window_size;
    size_t stride;
    size_t count;
    size_t write_index;
    size_t since_emit;
} imu_window_buffer_t;

void imu_window_buffer_init(imu_window_buffer_t *buffer, size_t window_size, size_t stride);
bool imu_window_buffer_push(imu_window_buffer_t *buffer, const imu_sample6_t *sample);
void imu_window_buffer_copy_latest(const imu_window_buffer_t *buffer, float out[256][6]);

#ifdef __cplusplus
}
#endif
