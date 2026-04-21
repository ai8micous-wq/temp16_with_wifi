#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "app_config.h"
#include "app_events.h"
#include "app_types.h"
#include "board.h"
#include "lcd_proto.h"
#include "mqtt_service.h"
#include "nvs_config.h"
#include "storage_service.h"
#include "temp_sample.h"
#include "wifi_service.h"
#include "factory_info.h"

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static const char *TAG = "app_main";
static EventGroupHandle_t s_event_group;
static system_config_t s_sys_cfg;
static channel_label_t s_labels[APP_CHANNEL_COUNT];
static channel_alarm_cfg_t s_alarms[APP_CHANNEL_COUNT];

#define WIFI_READY_BIT BIT0
#define MQTT_READY_BIT BIT1
#define PROV_RESET_HOLD_MS 2000
#define STATUS_TASK_PERIOD_MS 100

static void update_status_led(void)
{
    if (!wifi_service_is_provisioned()) {
        board_set_status_led_mode(wifi_service_is_provisioning() ?
                                      BOARD_STATUS_LED_PROVISIONING :
                                      BOARD_STATUS_LED_UNPROVISIONED);
        return;
    }

    if (wifi_service_is_provisioning()) {
        board_set_status_led_mode(BOARD_STATUS_LED_PROVISIONING);
        return;
    }

    if (wifi_service_is_connected() && mqtt_service_is_connected()) {
        board_set_status_led_mode(BOARD_STATUS_LED_ONLINE);
        return;
    }

    board_set_status_led_mode(BOARD_STATUS_LED_OFF);
}

static void publish_config_once(void)
{
    mqtt_service_publish_full_config(&s_sys_cfg, s_labels, s_alarms);
}

static void app_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != APP_EVENT) return;
    switch ((app_event_id_t)id) {
    case APP_EVENT_FORCE_REPORT:
        mqtt_service_publish_latest();
        break;
    case APP_EVENT_CONFIG_CHANGED:
        temp_sample_update_config(s_alarms);
        nvs_config_save_all(&s_sys_cfg, s_labels, s_alarms);
        lcd_proto_push_full_config(s_labels, s_alarms);
        publish_config_once();
        break;
    case APP_EVENT_TIME_SYNC_NEEDED:
        mqtt_service_request_time_sync();
        break;
    case APP_EVENT_FACTORY_RESET:
        ESP_LOGW(TAG, "Factory reset requested");
        nvs_config_factory_reset();
        esp_restart();
        break;
    default:
        break;
    }
}

static void status_task(void *arg)
{
    bool cfg_published = false;
    uint32_t hold_ms = 0;
    bool reset_fired = false;
    bool time_sync_requested_since_connect = false;
    TickType_t last_time_sync_request_tick = 0;

    while (1) {
        update_status_led();

        if (board_is_prov_reset_pressed()) {
            if (hold_ms < PROV_RESET_HOLD_MS) {
                hold_ms += STATUS_TASK_PERIOD_MS;
            }
            if (!reset_fired && hold_ms >= PROV_RESET_HOLD_MS) {
                reset_fired = true;
                ESP_LOGW(TAG, "Provision reset button held for %u ms, clearing provisioning", hold_ms);
                board_set_status_led_mode(BOARD_STATUS_LED_PROVISIONING);
                wifi_service_reset_provisioning_and_restart();
            }
        } else {
            hold_ms = 0;
            reset_fired = false;
        }

        if (wifi_service_is_connected()) {
            xEventGroupSetBits(s_event_group, WIFI_READY_BIT);
        } else {
            xEventGroupClearBits(s_event_group, WIFI_READY_BIT);
        }
        if ((xEventGroupGetBits(s_event_group) & WIFI_READY_BIT) && !mqtt_service_is_connected()) {
            mqtt_service_start(&s_sys_cfg);
        }
        if (mqtt_service_is_connected()) {
            xEventGroupSetBits(s_event_group, MQTT_READY_BIT);
            if (!cfg_published) {
                publish_config_once();
                mqtt_service_publish_status("running");
                cfg_published = true;
            }
            if (!time_sync_requested_since_connect) {
                if (mqtt_service_request_time_sync() == ESP_OK) {
                    time_sync_requested_since_connect = true;
                    last_time_sync_request_tick = xTaskGetTickCount();
                    ESP_LOGI(TAG, "requested initial time sync after MQTT connect");
                }
            } else if (s_sys_cfg.time_sync_interval_s > 0) {
                TickType_t now = xTaskGetTickCount();
                TickType_t interval_ticks = pdMS_TO_TICKS(s_sys_cfg.time_sync_interval_s * 1000UL);
                if ((now - last_time_sync_request_tick) >= interval_ticks) {
                    if (mqtt_service_request_time_sync() == ESP_OK) {
                        last_time_sync_request_tick = now;
                        ESP_LOGI(TAG, "requested periodic time sync, last_sync_ts=%lu",
                                 (unsigned long)mqtt_service_get_last_time_sync_ts());
                    }
                }
            }
        } else {
            xEventGroupClearBits(s_event_group, MQTT_READY_BIT);
            cfg_published = false;
            time_sync_requested_since_connect = false;
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_TASK_PERIOD_MS));
    }
}

static void ui_task(void *arg)
{
    temp_frame_t frame;
    while (1) {
        if (temp_sample_get_latest(&frame)) {
            lcd_proto_push_temperatures(&frame);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void mqtt_publish_task(void *arg)
{
    uint16_t last_cached_seq = 0;

    while (1) {
        temp_frame_t frame;
        if (temp_sample_get_latest(&frame) && frame.seq != 0 && frame.seq != last_cached_seq) {
            esp_err_t err = temp_sample_force_publish();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "cache latest sample failed seq=%u err=%s", frame.seq, esp_err_to_name(err));
            } else {
                last_cached_seq = frame.seq;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(APP_UPLOAD_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, app_event_handler, NULL));

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(factory_info_init());
    ESP_ERROR_CHECK(nvs_config_init());
    ESP_ERROR_CHECK(nvs_config_load_all(&s_sys_cfg, s_labels, s_alarms));
    ESP_ERROR_CHECK(storage_service_init());
    lcd_proto_bind_config(&s_sys_cfg, s_labels, s_alarms);
    mqtt_service_bind_config(&s_sys_cfg, s_labels, s_alarms);
    ESP_ERROR_CHECK(lcd_proto_init());
    ESP_ERROR_CHECK(temp_sample_init(s_alarms));
    ESP_ERROR_CHECK(temp_sample_start());
    ESP_ERROR_CHECK(wifi_service_start(&s_sys_cfg));

    xTaskCreate(status_task, "status_task", 4096, NULL, 4, NULL);
    xTaskCreate(ui_task, "ui_task", 4096, NULL, 3, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_pub", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System started. FW=%s", APP_FW_VERSION);
}
