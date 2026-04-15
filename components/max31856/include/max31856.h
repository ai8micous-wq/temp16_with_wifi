#pragma once
#include "esp_err.h"
#include "app_types.h"

typedef struct {
    uint8_t avg_sel;
    bool filter_50hz;
} max31856_cfg_t;

esp_err_t max31856_bus_init(void);
esp_err_t max31856_init_channel(uint8_t channel, const max31856_cfg_t *cfg);
esp_err_t max31856_read_channel(uint8_t channel, channel_runtime_t *out);
