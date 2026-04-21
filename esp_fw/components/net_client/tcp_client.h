#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void tcp_client_encode_frame_header(size_t len, uint8_t header[4]);
esp_err_t tcp_client_encode_frame_checked(size_t len, uint8_t header[4]);

esp_err_t tcp_client_configure(const char *server_ip, uint16_t server_port);
esp_err_t tcp_client_connect(void);
esp_err_t tcp_client_disconnect(void);
bool tcp_client_is_connected(void);
esp_err_t tcp_client_send_frame(const char *json, size_t len);
esp_err_t tcp_client_receive_frame(char *buffer, size_t buffer_len, size_t *out_len, int timeout_ms);

#ifdef __cplusplus
}
#endif
