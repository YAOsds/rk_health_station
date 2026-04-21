#include "tcp_client.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "TCP_CLIENT";
static int s_socket_fd = -1;
static char s_server_ip[64] = {0};
static uint16_t s_server_port = 0;

static esp_err_t recv_exact(uint8_t *buffer, size_t len)
{
    size_t received_total = 0;

    while (received_total < len) {
        int received = recv(s_socket_fd, buffer + received_total, len - received_total, 0);
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += (size_t)received;
    }

    return ESP_OK;
}

void tcp_client_encode_frame_header(size_t len, uint8_t header[4])
{
    header[0] = (uint8_t)((len >> 24) & 0xFF);
    header[1] = (uint8_t)((len >> 16) & 0xFF);
    header[2] = (uint8_t)((len >> 8) & 0xFF);
    header[3] = (uint8_t)(len & 0xFF);
}

esp_err_t tcp_client_encode_frame_checked(size_t len, uint8_t header[4])
{
    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0 || len > UINT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    tcp_client_encode_frame_header(len, header);
    return ESP_OK;
}

esp_err_t tcp_client_configure(const char *server_ip, uint16_t server_port)
{
    if (server_ip == NULL || server_ip[0] == '\0' || server_port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(s_server_ip, 0, sizeof(s_server_ip));
    strncpy(s_server_ip, server_ip, sizeof(s_server_ip) - 1);
    s_server_port = server_port;
    return ESP_OK;
}

esp_err_t tcp_client_connect(void)
{
    struct sockaddr_in server_addr;

    if (s_server_ip[0] == '\0' || s_server_port == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_socket_fd >= 0) {
        return ESP_OK;
    }

    s_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_socket_fd < 0) {
        ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
        s_socket_fd = -1;
        return ESP_FAIL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s_server_port);
    if (inet_pton(AF_INET, s_server_ip, &server_addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "invalid server ip: %s", s_server_ip);
        tcp_client_disconnect();
        return ESP_ERR_INVALID_ARG;
    }

    if (connect(s_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "connect failed: errno=%d", errno);
        tcp_client_disconnect();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "connected to %s:%u", s_server_ip, (unsigned)s_server_port);
    return ESP_OK;
}

esp_err_t tcp_client_disconnect(void)
{
    if (s_socket_fd >= 0) {
        shutdown(s_socket_fd, SHUT_RDWR);
        close(s_socket_fd);
        s_socket_fd = -1;
    }
    return ESP_OK;
}

bool tcp_client_is_connected(void)
{
    return s_socket_fd >= 0;
}

esp_err_t tcp_client_send_frame(const char *json, size_t len)
{
    uint8_t header[4];
    size_t sent_total = 0;

    if (json == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tcp_client_connect() != ESP_OK) {
        return ESP_FAIL;
    }
    if (tcp_client_encode_frame_checked(len, header) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (send(s_socket_fd, header, sizeof(header), 0) != (ssize_t)sizeof(header)) {
        ESP_LOGE(TAG, "send header failed: errno=%d", errno);
        tcp_client_disconnect();
        return ESP_FAIL;
    }

    while (sent_total < len) {
        int sent = send(s_socket_fd, json + sent_total, len - sent_total, 0);
        if (sent <= 0) {
            ESP_LOGE(TAG, "send payload failed: errno=%d", errno);
            tcp_client_disconnect();
            return ESP_FAIL;
        }
        sent_total += (size_t)sent;
    }

    return ESP_OK;
}

esp_err_t tcp_client_receive_frame(char *buffer, size_t buffer_len, size_t *out_len, int timeout_ms)
{
    uint8_t header[4];
    uint32_t frame_len;
    struct timeval timeout;

    if (buffer == NULL || buffer_len == 0 || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tcp_client_connect() != ESP_OK) {
        return ESP_FAIL;
    }

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (recv_exact(header, sizeof(header)) != ESP_OK) {
        ESP_LOGW(TAG, "receive header failed");
        tcp_client_disconnect();
        return ESP_FAIL;
    }

    frame_len = ((uint32_t)header[0] << 24)
        | ((uint32_t)header[1] << 16)
        | ((uint32_t)header[2] << 8)
        | (uint32_t)header[3];
    if (frame_len == 0 || frame_len >= buffer_len) {
        ESP_LOGE(TAG, "invalid frame length: %u", (unsigned)frame_len);
        tcp_client_disconnect();
        return ESP_ERR_INVALID_SIZE;
    }

    if (recv_exact((uint8_t *)buffer, frame_len) != ESP_OK) {
        ESP_LOGW(TAG, "receive payload failed");
        tcp_client_disconnect();
        return ESP_FAIL;
    }

    buffer[frame_len] = '\0';
    *out_len = frame_len;
    return ESP_OK;
}
