#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

typedef enum {
    TEMP_FAULT_NORMAL = 0,
    TEMP_FAULT_OPEN = 1,
    TEMP_FAULT_HIGH = 2,
    TEMP_FAULT_LOW = 3,
    TEMP_FAULT_COMM = 4,
    TEMP_FAULT_INVALID = 5,
} temp_fault_t;

typedef enum {
    CH_STATUS_DISABLED = 0,
    CH_STATUS_ENABLED = 1,
} channel_enable_t;

typedef struct {
    bool alarm_en;
    int16_t high_x10;
    int16_t low_x10;
} channel_alarm_cfg_t;

typedef struct {
    char device_id[32];
    char device_name[APP_DEVICE_NAME_MAX];
    char wifi_ssid[APP_WIFI_SSID_MAX];
    char wifi_pass[APP_WIFI_PASS_MAX];
    char mqtt_uri[APP_MQTT_URI_MAX];
    char mqtt_user[APP_MQTT_USER_MAX];
    char mqtt_pass[APP_MQTT_PASS_MAX];
    uint32_t report_interval_s;
    uint32_t time_sync_interval_s;
} system_config_t;

typedef struct {
    char label[APP_LABEL_MAX_BYTES];
} channel_label_t;

typedef struct {
    channel_enable_t enabled;
    int16_t temp_x10;
    temp_fault_t fault;
} channel_runtime_t;

typedef struct {
    uint32_t ts;
    int16_t temp_x10[APP_CHANNEL_COUNT];
    uint8_t fault[APP_CHANNEL_COUNT];
    uint16_t seq;
    uint16_t crc16;
} __attribute__((packed)) temp_record_t;

typedef struct {
    uint32_t ts;
    uint16_t seq;
    channel_runtime_t channels[APP_CHANNEL_COUNT];
} temp_frame_t;

typedef struct {
    uint32_t latest_ts;
    uint16_t latest_seq;
    bool wifi_ready;
    bool mqtt_ready;
} app_runtime_state_t;
