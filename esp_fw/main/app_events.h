#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_VITALS_READY = BIT0,
    APP_EVENT_LINK_READY = BIT1,
    APP_EVENT_PENDING_APPROVAL = BIT2,
    APP_EVENT_REJECTED = BIT3,
    APP_EVENT_SENSOR_FAULT = BIT4,
} app_event_t;
