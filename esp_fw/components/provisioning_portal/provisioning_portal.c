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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "provisioning_html.h"
#include "provisioning_scan.h"
#include "provisioning_status.h"

static const char *TAG = "PROVISION";
enum {
    PROVISION_SCAN_LIMIT = 20,
    PROVISION_SCAN_JSON_SIZE = 2048,
};

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static bool s_net_ready;
static bool s_wifi_started;
static bool s_wifi_events_registered;
static esp_event_handler_instance_t s_wifi_event_instance;
static TaskHandle_t s_heartbeat_task;
static SemaphoreHandle_t s_runtime_mutex;
static rk_provisioning_status_t s_runtime;

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

static esp_err_t ensure_runtime_mutex(void)
{
    if (s_runtime_mutex != NULL) {
        return ESP_OK;
    }

    s_runtime_mutex = xSemaphoreCreateMutex();
    return s_runtime_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void snapshot_runtime(rk_provisioning_status_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    if (s_runtime_mutex == NULL || xSemaphoreTake(s_runtime_mutex, portMAX_DELAY) != pdTRUE) {
        memset(snapshot, 0, sizeof(*snapshot));
        return;
    }

    *snapshot = s_runtime;
    xSemaphoreGive(s_runtime_mutex);
}

static void provisioning_heartbeat_task(void *arg)
{
    char message[160];
    rk_provisioning_status_t snapshot;

    (void)arg;

    while (true) {
        snapshot_runtime(&snapshot);
        if (rk_provisioning_format_heartbeat(&snapshot, message, sizeof(message))) {
            ESP_LOGI(TAG, "%s", message);
        }
        vTaskDelay(pdMS_TO_TICKS(RK_PROVISIONING_HEARTBEAT_INTERVAL_MS));
    }
}

static esp_err_t start_heartbeat_task(void)
{
    if (s_heartbeat_task != NULL) {
        return ESP_OK;
    }

    if (xTaskCreate(provisioning_heartbeat_task, "prov_heartbeat", 3072, NULL, 5, &s_heartbeat_task) != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void stop_heartbeat_task(void)
{
    if (s_heartbeat_task != NULL) {
        vTaskDelete(s_heartbeat_task);
        s_heartbeat_task = NULL;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    size_t clients = 0;

    (void)arg;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;

        if (s_runtime_mutex != NULL && xSemaphoreTake(s_runtime_mutex, portMAX_DELAY) == pdTRUE) {
            rk_provisioning_status_note_client_event(&s_runtime, true);
            clients = s_runtime.connected_clients;
            xSemaphoreGive(s_runtime_mutex);
        }

        ESP_LOGI(TAG, "provisioning client connected aid=%d clients=%u",
            event != NULL ? event->aid : -1,
            (unsigned)clients);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)event_data;

        if (s_runtime_mutex != NULL && xSemaphoreTake(s_runtime_mutex, portMAX_DELAY) == pdTRUE) {
            rk_provisioning_status_note_client_event(&s_runtime, false);
            clients = s_runtime.connected_clients;
            xSemaphoreGive(s_runtime_mutex);
        }

        ESP_LOGI(TAG, "provisioning client disconnected aid=%d clients=%u",
            event != NULL ? event->aid : -1,
            (unsigned)clients);
    }
}

static void update_scan_runtime(const rk_provisioning_scan_ap_t *aps, size_t normalized)
{
    if (s_runtime_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_runtime_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    rk_provisioning_status_note_scan(&s_runtime, aps, normalized);
    xSemaphoreGive(s_runtime_mutex);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    wifi_ap_record_t *ap_records = NULL;
    rk_provisioning_scan_ap_t *input = NULL;
    rk_provisioning_scan_ap_t *output = NULL;
    uint16_t ap_count = PROVISION_SCAN_LIMIT;
    size_t normalized;
    uint16_t i;
    char *json = NULL;

    ap_records = calloc(PROVISION_SCAN_LIMIT, sizeof(*ap_records));
    input = calloc(PROVISION_SCAN_LIMIT, sizeof(*input));
    output = calloc(PROVISION_SCAN_LIMIT, sizeof(*output));
    json = calloc(1, PROVISION_SCAN_JSON_SIZE);
    if (ap_records == NULL || input == NULL || output == NULL || json == NULL) {
        free(json);
        free(output);
        free(input);
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan start failed");
        free(json);
        free(output);
        free(input);
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    if (esp_wifi_scan_get_ap_records(&ap_count, ap_records) != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan collect failed");
        free(json);
        free(output);
        free(input);
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    for (i = 0; i < ap_count; ++i) {
        strncpy(input[i].ssid, (const char *)ap_records[i].ssid, sizeof(input[i].ssid) - 1);
        input[i].rssi = ap_records[i].rssi;
    }

    normalized = rk_provisioning_normalize_scan_results(input, ap_count, output, PROVISION_SCAN_LIMIT);
    if (!rk_provisioning_build_scan_json(output, normalized, json, PROVISION_SCAN_JSON_SIZE)) {
        free(json);
        free(output);
        free(input);
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    update_scan_runtime(output, normalized);

    if (normalized > 0) {
        ESP_LOGI(TAG,
            "scan complete aps=%u normalized=%u strongest=%s rssi=%d",
            (unsigned)ap_count,
            (unsigned)normalized,
            output[0].ssid,
            output[0].rssi);
    } else {
        ESP_LOGI(TAG, "scan complete aps=%u normalized=0", (unsigned)ap_count);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    free(output);
    free(input);
    free(ap_records);
    return ESP_OK;
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

    ret = ensure_runtime_mutex();
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

    if (!s_wifi_events_registered) {
        ret = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_event_instance);
        if (ret != ESP_OK) {
            return ret;
        }
        s_wifi_events_registered = true;
    }

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "RKHealth-%02X%02X", mac[4], mac[5]);

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, "rkhealth01", sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    memset(&s_runtime, 0, sizeof(s_runtime));

    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
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
    httpd_uri_t scan_uri = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };
    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    esp_err_t ret;

    /* /scan triggers Wi-Fi stack work; keep ample headroom for the HTTP server task. */
    config.stack_size = 8192;

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
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &save_uri);

    ret = start_heartbeat_task();
    if (ret != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "provisioning portal started on http://192.168.4.1/");
    return ESP_OK;
}

esp_err_t provisioning_portal_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    stop_heartbeat_task();

    if (s_wifi_events_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        s_wifi_events_registered = false;
    }

    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }

    if (s_runtime_mutex != NULL) {
        vSemaphoreDelete(s_runtime_mutex);
        s_runtime_mutex = NULL;
    }

    return ESP_OK;
}
