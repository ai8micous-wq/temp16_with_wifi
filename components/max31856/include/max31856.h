#pragma once
#include "esp_err.h"
#include "app_types.h"

typedef enum {
    MAX31856_TC_TYPE_B = 0x0,
    MAX31856_TC_TYPE_E = 0x1,
    MAX31856_TC_TYPE_J = 0x2,
    MAX31856_TC_TYPE_K = 0x3,
    MAX31856_TC_TYPE_N = 0x4,
    MAX31856_TC_TYPE_R = 0x5,
    MAX31856_TC_TYPE_S = 0x6,
    MAX31856_TC_TYPE_T = 0x7,
} max31856_tc_type_t;

typedef struct {
    uint8_t avg_sel;
    bool filter_50hz;
    max31856_tc_type_t tc_type;
} max31856_cfg_t;

typedef struct {
    uint8_t raw_temp[3];
    uint8_t raw_status;
} max31856_debug_snapshot_t;

esp_err_t max31856_bus_init(void);
esp_err_t max31856_init_channel(uint8_t channel, const max31856_cfg_t *cfg);
esp_err_t max31856_read_channel(uint8_t channel, channel_runtime_t *out);
bool max31856_get_last_debug_snapshot(uint8_t channel, max31856_debug_snapshot_t *out);
