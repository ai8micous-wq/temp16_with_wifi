#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "app_types.h"

esp_err_t wifi_service_start(const system_config_t *cfg);
bool wifi_service_is_connected(void);
int8_t wifi_service_get_rssi(void);
