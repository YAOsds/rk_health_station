#include "auth_client.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "tcp_client.h"

static const char *TAG = "AUTH_CLIENT";

static bool read_payload_string(cJSON *root, const char *key, char *buffer, size_t buffer_len)
{
    cJSON *payload;
    cJSON *value;

    if (root == NULL || key == NULL || buffer == NULL || buffer_len == 0) {
        return false;
    }

    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    value = payload != NULL ? cJSON_GetObjectItemCaseSensitive(payload, key) : NULL;
    if (!cJSON_IsString(value) || value->valuestring == NULL) {
        return false;
    }

    strncpy(buffer, value->valuestring, buffer_len - 1);
    buffer[buffer_len - 1] = '\0';
    return true;
}

static bool read_result_string(cJSON *root, char *buffer, size_t buffer_len)
{
    return read_payload_string(root, "result", buffer, buffer_len);
}

static esp_err_t build_mac_string(char *buffer, size_t buffer_len)
{
    uint8_t mac[6] = {0};

    if (buffer == NULL || buffer_len < 18) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buffer, buffer_len, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

static esp_err_t compute_hmac_hex(const rk_device_config_t *config, const char *server_nonce,
    const char *client_nonce, int64_t ts, char *proof_hex, size_t proof_hex_len)
{
    unsigned char digest[32];
    unsigned char message[256];
    size_t message_len;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (config == NULL || server_nonce == NULL || client_nonce == NULL
        || proof_hex == NULL || proof_hex_len < 65 || md_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    message_len = (size_t)snprintf((char *)message, sizeof(message), "%s%s%s%lld",
        config->device_id, server_nonce, client_nonce, (long long)ts);
    if (message_len == 0 || message_len >= sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (mbedtls_md_hmac(md_info, (const unsigned char *)config->device_secret,
            strlen(config->device_secret), message, message_len, digest) != 0) {
        return ESP_FAIL;
    }

    for (size_t index = 0; index < sizeof(digest); ++index) {
        snprintf(proof_hex + (index * 2), proof_hex_len - (index * 2), "%02x", digest[index]);
    }
    proof_hex[64] = '\0';
    return ESP_OK;
}

static esp_err_t send_auth_hello(const rk_device_config_t *config, const char *firmware_version)
{
    char frame[768];
    char mac[18] = {0};
    int64_t ts = esp_timer_get_time() / 1000000;

    if (build_mac_string(mac, sizeof(mac)) != ESP_OK) {
        return ESP_FAIL;
    }

    snprintf(frame, sizeof(frame),
        "{\"ver\":1,\"type\":\"auth_hello\",\"seq\":1,\"ts\":%lld,\"device_id\":\"%s\","
        "\"payload\":{\"device_name\":\"%s\",\"firmware_version\":\"%s\","
        "\"hardware_model\":\"esp32s3\",\"mac\":\"%s\"}}",
        (long long)ts,
        config->device_id,
        config->device_name,
        firmware_version != NULL ? firmware_version : "esp-fw",
        mac);
    return tcp_client_send_frame(frame, strlen(frame));
}

static esp_err_t send_auth_proof(const rk_device_config_t *config, const char *server_nonce)
{
    char frame[768];
    char client_nonce[33];
    char proof_hex[65] = {0};
    int64_t ts = esp_timer_get_time() / 1000000;

    snprintf(client_nonce, sizeof(client_nonce), "%08" PRIx32 "%08" PRIx32,
        (uint32_t)esp_random(), (uint32_t)esp_random());
    if (compute_hmac_hex(config, server_nonce, client_nonce, ts, proof_hex, sizeof(proof_hex)) != ESP_OK) {
        return ESP_FAIL;
    }

    snprintf(frame, sizeof(frame),
        "{\"ver\":1,\"type\":\"auth_proof\",\"seq\":2,\"ts\":%lld,\"device_id\":\"%s\","
        "\"payload\":{\"client_nonce\":\"%s\",\"proof\":\"%s\",\"ts\":%lld}}",
        (long long)ts,
        config->device_id,
        client_nonce,
        proof_hex,
        (long long)ts);
    return tcp_client_send_frame(frame, strlen(frame));
}

esp_err_t auth_client_authenticate(
    const rk_device_config_t *config,
    const char *firmware_version,
    auth_client_result_t *result)
{
    char rx_buffer[1024];
    size_t rx_len = 0;
    cJSON *root = NULL;
    cJSON *type = NULL;
    char server_nonce[80] = {0};
    char auth_result[64] = {0};
    esp_err_t ret;

    if (config == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *result = AUTH_RETRY;
    ret = send_auth_hello(config, firmware_version);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = tcp_client_receive_frame(rx_buffer, sizeof(rx_buffer), &rx_len, 5000);
    if (ret != ESP_OK) {
        return ret;
    }

    root = cJSON_ParseWithLength(rx_buffer, rx_len);
    if (root == NULL) {
        return ESP_FAIL;
    }

    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "auth_challenge") == 0) {
        if (!read_payload_string(root, "server_nonce", server_nonce, sizeof(server_nonce))) {
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        cJSON_Delete(root);

        ret = send_auth_proof(config, server_nonce);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = tcp_client_receive_frame(rx_buffer, sizeof(rx_buffer), &rx_len, 5000);
        if (ret != ESP_OK) {
            return ret;
        }

        root = cJSON_ParseWithLength(rx_buffer, rx_len);
        if (root == NULL) {
            return ESP_FAIL;
        }
    }

    if (!read_result_string(root, auth_result, sizeof(auth_result))) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (strcmp(auth_result, "ok") == 0) {
        *result = AUTH_OK;
    } else if (strcmp(auth_result, "registration_required") == 0) {
        *result = AUTH_PENDING;
    } else if (strcmp(auth_result, "rejected") == 0) {
        *result = AUTH_REJECTED;
    } else {
        *result = AUTH_RETRY;
    }

    ESP_LOGI(TAG, "auth result for %s: %s", config->device_id, auth_result);
    cJSON_Delete(root);
    return ESP_OK;
}
