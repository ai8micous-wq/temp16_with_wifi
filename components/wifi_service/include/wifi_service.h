#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_types.h"

esp_err_t wifi_service_start(const system_config_t *cfg);
esp_err_t wifi_service_reset_provisioning_and_restart(void);
bool wifi_service_is_connected(void);
bool wifi_service_is_provisioned(void);
bool wifi_service_is_provisioning(void);
int8_t wifi_service_get_rssi(void);
esp_err_t wifi_service_get_ip_string(char *buf, size_t len);
