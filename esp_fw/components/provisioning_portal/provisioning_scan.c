#include "provisioning_scan.h"

#include <stdio.h>
#include <string.h>

static void copy_ap(rk_provisioning_scan_ap_t *dst, const rk_provisioning_scan_ap_t *src)
{
    memset(dst, 0, sizeof(*dst));
    strncpy(dst->ssid, src->ssid, sizeof(dst->ssid) - 1);
    dst->rssi = src->rssi;
}

static void sort_by_rssi_desc(rk_provisioning_scan_ap_t *aps, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        size_t j;

        for (j = i + 1; j < count; ++j) {
            if (aps[j].rssi > aps[i].rssi) {
                rk_provisioning_scan_ap_t tmp = aps[i];
                aps[i] = aps[j];
                aps[j] = tmp;
            }
        }
    }
}

static bool append_json_escaped(char *json, size_t json_size, size_t *used, const char *src)
{
    while (*src != '\0') {
        int written;
        const char *replacement = NULL;

        switch (*src) {
        case '\\':
            replacement = "\\\\";
            break;
        case '"':
            replacement = "\\\"";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            break;
        }

        if (replacement != NULL) {
            written = snprintf(json + *used, json_size - *used, "%s", replacement);
        } else {
            written = snprintf(json + *used, json_size - *used, "%c", *src);
        }

        if (written < 0 || (size_t)written >= json_size - *used) {
            return false;
        }

        *used += (size_t)written;
        ++src;
    }

    return true;
}

size_t rk_provisioning_normalize_scan_results(
    const rk_provisioning_scan_ap_t *input,
    size_t input_count,
    rk_provisioning_scan_ap_t *output,
    size_t output_capacity)
{
    size_t output_count = 0;
    size_t i;

    for (i = 0; i < input_count; ++i) {
        size_t existing = output_count;
        size_t j;

        if (input[i].ssid[0] == '\0') {
            continue;
        }

        for (j = 0; j < output_count; ++j) {
            if (strcmp(output[j].ssid, input[i].ssid) == 0) {
                existing = j;
                break;
            }
        }

        if (existing < output_count) {
            if (input[i].rssi > output[existing].rssi) {
                output[existing].rssi = input[i].rssi;
            }
            continue;
        }

        if (output_count >= output_capacity) {
            break;
        }

        copy_ap(&output[output_count], &input[i]);
        ++output_count;
    }

    sort_by_rssi_desc(output, output_count);
    return output_count;
}

bool rk_provisioning_build_scan_json(
    const rk_provisioning_scan_ap_t *aps,
    size_t ap_count,
    char *json,
    size_t json_size)
{
    size_t used;
    size_t i;

    if (json == NULL || json_size < 3) {
        return false;
    }

    json[0] = '\0';
    used = (size_t)snprintf(json, json_size, "[");
    if (used >= json_size) {
        return false;
    }

    for (i = 0; i < ap_count; ++i) {
        int written = snprintf(json + used, json_size - used,
            "%s{\"ssid\":\"", i == 0 ? "" : ",");
        if (written < 0 || (size_t)written >= json_size - used) {
            return false;
        }
        used += (size_t)written;

        if (!append_json_escaped(json, json_size, &used, aps[i].ssid)) {
            return false;
        }

        written = snprintf(json + used, json_size - used,
            "\",\"rssi\":%d}", aps[i].rssi);
        if (written < 0 || (size_t)written >= json_size - used) {
            return false;
        }
        used += (size_t)written;
    }

    if (snprintf(json + used, json_size - used, "]") >= (int)(json_size - used)) {
        return false;
    }

    return true;
}
