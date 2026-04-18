#pragma once
#include "esp_err.h"
#include "app_types.h"

esp_err_t nvs_config_init(void);
esp_err_t nvs_config_load_all(system_config_t *sys, channel_label_t labels[APP_CHANNEL_COUNT], channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
esp_err_t nvs_config_save_all(const system_config_t *sys, const channel_label_t labels[APP_CHANNEL_COUNT], const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
esp_err_t nvs_config_factory_reset(void);
