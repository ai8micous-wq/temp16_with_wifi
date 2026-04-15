#pragma once
#include "esp_err.h"

esp_err_t cs_mux_init(void);
esp_err_t cs_mux_select(uint8_t channel); // 0-15
esp_err_t cs_mux_deselect_all(void);
