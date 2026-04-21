#include "provisioning_portal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "provisioning_html.h"

static const char *TAG = "PROVISION";
static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static bool s_net_ready;
static bool s_wifi_started;

static void url_decode(char *buffer)
{
    char *src = buffer;
    char *dst = buffer;

    while (*src != '\0') {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            unsigned int value = 0;
            sscanf(src + 1, "%2x", &value);
            *dst++ = (char)value;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

static bool extract_param(const char *body, const char *key, char *buffer, size_t buffer_len)
{
    char pattern[64];
    const char *start;
    size_t len = 0;

    if (body == NULL || key == NULL || buffer == NULL || buffer_len == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "%s=", key);
    start = strstr(body, pattern);
    if (start == NULL) {
        return false;
    }

    start += strlen(pattern);
    while (start[len] != '\0' && start[len] != '&' && len + 1 < buffer_len) {
        buffer[len] = start[len];
        ++len;
    }
    if (start[len] != '\0' && start[len] != '&') {
        return false;
    }

    buffer[len] = '\0';
    url_decode(buffer);
    return true;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

esp_err_t provisioning_portal_parse_form(const char *body, rk_device_config_t *config)
{
    char port_buffer[16] = {0};
    long port_value;

    if (body == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rk_device_config_clear(config);
    if (!extract_param(body, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid))
        || !extract_param(body, "wifi_password", config->wifi_password, sizeof(config->wifi_password))
        || !extract_param(body, "server_ip", config->server_ip, sizeof(config->server_ip))
        || !extract_param(body, "server_port", port_buffer, sizeof(port_buffer))
        || !extract_param(body, "device_id", config->device_id, sizeof(config->device_id))
        || !extract_param(body, "device_name", config->device_name, sizeof(config->device_name))
        || !extract_param(body, "device_secret", config->device_secret, sizeof(config->device_secret))) {
        return ESP_ERR_INVALID_ARG;
    }

    port_value = strtol(port_buffer, NULL, 10);
    if (port_value < 1 || port_value > 65535) {
        return ESP_ERR_INVALID_ARG;
    }
    config->server_port = (int)port_value;

    return rk_device_config_is_complete(config) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, RK_PROVISIONING_HTML);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[768];
    int received;
    rk_device_config_t config = {0};
    esp_err_t ret;

    memset(body, 0, sizeof(body));
    received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }

    ret = provisioning_portal_parse_form(body, &config);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid provisioning form");
        return ret;
    }

    ret = rk_device_config_save(&config);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ret;
    }

    httpd_resp_sendstr(req, "saved, restarting");
    xTaskCreate(restart_task, "prov_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t ensure_network_stack(void)
{
    esp_err_t ret;

    if (s_net_ready) {
        return ESP_OK;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_net_ready = true;
    return ESP_OK;
}

static esp_err_t start_softap(void)
{
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};
    uint8_t mac[6] = {0};
    char ssid[32];
    esp_err_t ret;

    ret = ensure_network_stack();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            return ESP_FAIL;
        }
    }

    if (!s_wifi_started) {
        ret = esp_wifi_init(&wifi_init);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_wifi_started = true;
    }

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "RKHealth-%02X%02X", mac[4], mac[5]);

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, "rkhealth01", sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set ap mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set ap config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "softap ready: ssid=%s password=rkhealth01", ssid);
    return ESP_OK;
}

esp_err_t provisioning_portal_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    esp_err_t ret;

    if (s_server != NULL) {
        return ESP_OK;
    }

    ret = start_softap();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &save_uri);
    ESP_LOGI(TAG, "provisioning portal started on http://192.168.4.1/");
    return ESP_OK;
}

esp_err_t provisioning_portal_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    if (s_wifi_started) {
        esp_wifi_stop();
    }

    return ESP_OK;
}
