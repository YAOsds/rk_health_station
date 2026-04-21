#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"

static const char *TAG = "WIFI_MGR";
static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;
static bool s_wifi_started;
static bool s_connected;
static int s_retry_count;
static int s_rssi;
static char s_ip_addr[16];

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_MAX_RETRY     5

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_rssi = 0;
        s_ip_addr[0] = '\0';
        if (s_retry_count < WIFI_MAX_RETRY) {
            ++s_retry_count;
            ESP_LOGW(TAG, "wifi disconnected, retry=%d", s_retry_count);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        s_connected = true;
        s_retry_count = 0;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "wifi connected ip=%s", s_ip_addr);
    }
}

static esp_err_t ensure_network_stack(void)
{
    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_start(const rk_device_config_t *config)
{
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};
    EventBits_t bits;
    esp_err_t ret;

    if (config == NULL || config->wifi_ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ensure_network_stack();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return ESP_FAIL;
        }
    }

    if (!s_wifi_started) {
        ret = esp_wifi_init(&wifi_init);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
        s_wifi_started = true;
    }

    strncpy((char *)wifi_config.sta.ssid, config->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.failure_retry_cnt = WIFI_MAX_RETRY;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry_count = 0;
    s_connected = false;
    s_rssi = 0;
    s_ip_addr[0] = '\0';

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set sta mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set sta config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(15000));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t wifi_manager_stop(void)
{
    if (s_wifi_started) {
        esp_wifi_stop();
        s_connected = false;
    }
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    wifi_ap_record_t ap_info;

    if (!s_connected) {
        return false;
    }

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_rssi = ap_info.rssi;
    }
    return true;
}

int wifi_manager_get_rssi(void)
{
    if (wifi_manager_is_connected()) {
        return s_rssi;
    }
    return 0;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_addr;
}
