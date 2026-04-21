#include "temp_sample.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_config.h"
#include "max31856.h"
#include "storage_service.h"

static const char *TAG = "temp_sample";
static temp_frame_t s_latest;
static SemaphoreHandle_t s_lock;
static channel_alarm_cfg_t s_alarm_cfg[APP_CHANNEL_COUNT];
static channel_runtime_t s_prev_channels[APP_CHANNEL_COUNT];
static bool s_prev_valid;

#define SAMPLE_DEBUG_FULL_DUMP_INTERVAL 10

static uint16_t crc16_simple(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
    }
    return crc;
}

static void log_channel_debug(uint16_t seq, int channel, const channel_runtime_t *runtime, esp_err_t read_err, bool force)
{
    bool changed = !s_prev_valid ||
                   s_prev_channels[channel].temp_x10 != runtime->temp_x10 ||
                   s_prev_channels[channel].fault != runtime->fault ||
                   s_prev_channels[channel].enabled != runtime->enabled;

    if (!force && read_err == ESP_OK && !changed && runtime->fault == TEMP_FAULT_NORMAL) {
        return;
    }

    max31856_debug_snapshot_t snap = {0};
    bool has_snap = max31856_get_last_debug_snapshot(channel, &snap);

    ESP_LOGI(TAG,
             "seq=%u ch=%02d err=%s temp_x10=%d fault=%d enabled=%d raw=0x%02X%02X%02X sr=0x%02X%s",
             seq,
             channel + 1,
             esp_err_to_name(read_err),
             runtime->temp_x10,
             runtime->fault,
             runtime->enabled,
             has_snap ? snap.raw_temp[0] : 0,
             has_snap ? snap.raw_temp[1] : 0,
             has_snap ? snap.raw_temp[2] : 0,
             has_snap ? snap.raw_status : 0,
             force ? " periodic" : (changed ? " changed" : " fault"));
}

static void sample_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint16_t seq = 0;
    while (1) {
        temp_frame_t frame = {0};
        frame.ts = (uint32_t)(esp_log_timestamp() / 1000);
        frame.seq = ++seq;
        bool force_log = (seq == 1) || ((seq % SAMPLE_DEBUG_FULL_DUMP_INTERVAL) == 0);
        for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
            if (s_alarm_cfg[i].enabled == CH_STATUS_DISABLED) {
                frame.channels[i].enabled = CH_STATUS_DISABLED;
                frame.channels[i].temp_x10 = 0;
                frame.channels[i].fault = TEMP_FAULT_INVALID;
                log_channel_debug(seq, i, &frame.channels[i], ESP_OK, force_log);
                continue;
            }

            esp_err_t read_err = max31856_read_channel(i, &frame.channels[i]);
            if (read_err != ESP_OK) {
                ESP_LOGW(TAG, "seq=%u ch=%02d read failed: %s", seq, i + 1, esp_err_to_name(read_err));
            }
            if (frame.channels[i].fault == TEMP_FAULT_NORMAL && s_alarm_cfg[i].alarm_en) {
                if (frame.channels[i].temp_x10 > s_alarm_cfg[i].high_x10) frame.channels[i].fault = TEMP_FAULT_HIGH;
                if (frame.channels[i].temp_x10 < s_alarm_cfg[i].low_x10) frame.channels[i].fault = TEMP_FAULT_LOW;
            }
            log_channel_debug(seq, i, &frame.channels[i], read_err, force_log);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_latest = frame;
        xSemaphoreGive(s_lock);
        memcpy(s_prev_channels, frame.channels, sizeof(s_prev_channels));
        s_prev_valid = true;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

esp_err_t temp_sample_init(const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    memcpy(s_alarm_cfg, alarms, sizeof(s_alarm_cfg));
    s_lock = xSemaphoreCreateMutex();
    return max31856_bus_init();
}

esp_err_t temp_sample_start(void)
{
    max31856_cfg_t cfg = {
        .avg_sel = 3,
        .filter_50hz = true,
        .tc_type = MAX31856_TC_TYPE_K,
    };
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        max31856_init_channel(i, &cfg);
    }
    xTaskCreate(sample_task, "sample_task", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "sample task started");
    return ESP_OK;
}

bool temp_sample_get_latest(temp_frame_t *out)
{
    if (!out || !s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_lock);
    return true;
}

esp_err_t temp_sample_force_publish(void)
{
    temp_record_t rec = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    rec.ts = s_latest.ts;
    rec.seq = s_latest.seq;
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        rec.temp_x10[i] = s_latest.channels[i].temp_x10;
        rec.fault[i] = s_latest.channels[i].fault;
    }
    xSemaphoreGive(s_lock);
    rec.crc16 = crc16_simple((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc16));
    return storage_service_append(&rec);
}

esp_err_t temp_sample_update_config(const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    if (!alarms) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_alarm_cfg, alarms, sizeof(s_alarm_cfg));
    return ESP_OK;
}
