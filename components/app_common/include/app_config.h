#pragma once

#define APP_PRODUCT_KEY                 "temp16"
#define APP_DEFAULT_DEVICE_ID           "temp16_240401_0001"
#define APP_DEFAULT_DEVICE_NAME         "temp16-monitor"
#define APP_FW_VERSION                  "0.1.0"
#define APP_HW_VERSION                  "A01"

#define APP_CHANNEL_COUNT               16
#define APP_LABEL_MAX_BYTES             24

#define APP_SAMPLE_PERIOD_MS            500
#define APP_UPLOAD_PERIOD_MS            1000
#define APP_TIME_SYNC_INTERVAL_S        (12 * 3600)

#define APP_WIFI_SSID_MAX               32
#define APP_WIFI_PASS_MAX               64
#define APP_MQTT_URI_MAX                128
#define APP_MQTT_USER_MAX               32
#define APP_MQTT_PASS_MAX               32
#define APP_DEVICE_NAME_MAX             32

#define APP_UART_LCD_BAUDRATE           9600
#define APP_UART_LCD_PORT               0

#define APP_MQTT_DEFAULT_URI            "mqtt://8.138.108.202:1883"
#define APP_MQTT_DEFAULT_USER           "mqttuser"
#define APP_MQTT_DEFAULT_PASS           "yuyou123"

#define APP_EVENT_BASE_NAME             "TEMP16_APP"
