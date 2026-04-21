#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "app_types.h"

esp_err_t temp_sample_init(const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
esp_err_t temp_sample_start(void);
bool temp_sample_get_latest(temp_frame_t *out);
esp_err_t temp_sample_force_publish(void);
esp_err_t temp_sample_update_config(const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
