#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "app_types.h"

esp_err_t mqtt_service_start(const system_config_t *cfg);
bool mqtt_service_is_connected(void);
esp_err_t mqtt_service_publish_latest(void);
esp_err_t mqtt_service_publish_full_config(const system_config_t *sys, const channel_label_t labels[APP_CHANNEL_COUNT], const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
esp_err_t mqtt_service_request_time_sync(void);
