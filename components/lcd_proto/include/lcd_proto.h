#pragma once
#include "esp_err.h"
#include "app_types.h"

void lcd_proto_bind_config(system_config_t *sys,
                           channel_label_t labels[APP_CHANNEL_COUNT],
                           channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT]);
esp_err_t lcd_proto_init(void);
esp_err_t lcd_proto_push_temperatures(const temp_frame_t *frame);
esp_err_t lcd_proto_push_full_config(const channel_label_t *labels, const channel_alarm_cfg_t *alarms);
