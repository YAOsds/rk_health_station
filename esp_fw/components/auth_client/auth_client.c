#include "auth_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "psa/crypto.h"
#include "tcp_client.h"

static const char *TAG = "AUTH_CLIENT";

static bool json_extract_string(const char *json, const char *key, char *buffer, size_t buffer_len)
{
    char pattern[64];
    const char *start;
    size_t len = 0;

    if (json == NULL || key == NULL || buffer == NULL || buffer_len == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    start = strstr(json, pattern);
    if (start == NULL) {
        return false;
    }

    start += strlen(pattern);
    while (start[len] != '\0' && start[len] != '"' && len + 1 < buffer_len) {
        if (start[len] == '\\' && start[len + 1] != '\0') {
            if (len + 2 >= buffer_len) {
                return false;
            }
            buffer[len] = start[len + 1];
            ++len;
            start += 1;
            continue;
        }
        buffer[len] = start[len];
        ++len;
    }

    if (start[len] != '"') {
        return false;
    }

    buffer[len] = '\0';
    return true;
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

esp_err_t auth_client_build_proof(const rk_device_config_t *config, const char *server_nonce,
    const char *client_nonce, int64_t ts, char *proof_hex, size_t proof_hex_len)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status;
    unsigned char digest[32];
    char message[256];
    size_t digest_len = 0;
    size_t message_len;

    if (config == NULL || server_nonce == NULL || client_nonce == NULL
        || proof_hex == NULL || proof_hex_len < 65) {
        return ESP_ERR_INVALID_ARG;
    }

    message_len = (size_t)snprintf(message, sizeof(message), "%s%s%s%lld",
        config->device_id,
        server_nonce,
        client_nonce,
        (long long)ts);
    if (message_len == 0 || message_len >= sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }

    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, strlen(config->device_secret) * 8U);
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_VOLATILE);

    status = psa_import_key(&attributes,
        (const uint8_t *)config->device_secret,
        strlen(config->device_secret),
        &key_id);
    if (status != PSA_SUCCESS) {
        psa_reset_key_attributes(&attributes);
        return ESP_FAIL;
    }

    status = psa_mac_compute(key_id,
        PSA_ALG_HMAC(PSA_ALG_SHA_256),
        (const uint8_t *)message,
        message_len,
        digest,
        sizeof(digest),
        &digest_len);
    psa_destroy_key(key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS || digest_len != sizeof(digest)) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < sizeof(digest); ++i) {
        snprintf(proof_hex + (i * 2), proof_hex_len - (i * 2), "%02x", digest[i]);
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
    if (auth_client_build_proof(config, server_nonce, client_nonce, ts, proof_hex, sizeof(proof_hex)) != ESP_OK) {
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

static esp_err_t parse_auth_result(const char *json, auth_client_state_t *state)
{
    char result[64] = {0};

    if (!json_extract_string(json, "result", result, sizeof(result))) {
        return ESP_FAIL;
    }

    if (strcmp(result, "ok") == 0) {
        *state = AUTH_STATE_AUTHENTICATED;
    } else if (strcmp(result, "registration_required") == 0) {
        *state = AUTH_STATE_PENDING;
    } else if (strcmp(result, "rejected") == 0) {
        *state = AUTH_STATE_REJECTED;
    } else {
        *state = AUTH_STATE_UNAUTHENTICATED;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "auth result=%s", result);
    return ESP_OK;
}

esp_err_t auth_client_authenticate(
    const rk_device_config_t *config,
    const char *firmware_version,
    auth_client_state_t *state)
{
    char rx_buffer[1024] = {0};
    char type[64] = {0};
    char server_nonce[96] = {0};
    size_t rx_len = 0;
    esp_err_t ret;

    if (config == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = AUTH_STATE_UNAUTHENTICATED;

    ret = send_auth_hello(config, firmware_version);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = tcp_client_receive_frame(rx_buffer, sizeof(rx_buffer), &rx_len, 5000);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!json_extract_string(rx_buffer, "type", type, sizeof(type))) {
        return ESP_FAIL;
    }

    if (strcmp(type, "auth_challenge") == 0) {
        if (!json_extract_string(rx_buffer, "server_nonce", server_nonce, sizeof(server_nonce))) {
            return ESP_FAIL;
        }

        ret = send_auth_proof(config, server_nonce);
        if (ret != ESP_OK) {
            return ret;
        }

        memset(rx_buffer, 0, sizeof(rx_buffer));
        ret = tcp_client_receive_frame(rx_buffer, sizeof(rx_buffer), &rx_len, 5000);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return parse_auth_result(rx_buffer, state);
}
