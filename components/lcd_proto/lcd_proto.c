#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "lcd_proto.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "app_events.h"
#include "board.h"

static const char *TAG = "lcd_proto";

static const uint8_t FRAME_HEAD = 0xEE;
static const uint8_t TAIL_BYTES[] = {0xFF, 0xFC, 0xFF, 0xFF};

static const uint16_t TEMP_IDS[16] = {
    0x0014, 0x0019, 0x001E, 0x0023, 0x0028, 0x002D, 0x0032, 0x0037,
    0x003C, 0x0041, 0x0046, 0x004B, 0x0050, 0x0055, 0x005A, 0x005F
};
static const uint16_t LABEL_IDS[16] = {
    0x0015, 0x001A, 0x001F, 0x0024, 0x0029, 0x002E, 0x0033, 0x0038,
    0x003D, 0x0042, 0x0047, 0x004C, 0x0051, 0x0056, 0x005B, 0x0060
};

enum {
    LCD_PAGE_MAIN = 0x0000,
    LCD_PAGE_CFG_1_8 = 0x0001,
    LCD_PAGE_CFG_9_16 = 0x0002,
};

enum {
    LCD_CMD_SET_TEXT = 0xB110,
    LCD_CMD_REPORT_CTRL = 0xB111,
};

enum {
    LCD_CTRL_TYPE_BINARY = 0x10,
    LCD_CTRL_TYPE_STRING = 0x11,
};

static system_config_t *s_sys_cfg;
static channel_label_t *s_labels;
static channel_alarm_cfg_t *s_alarms;

static int find_tail(const uint8_t *buf, size_t len)
{
    if (len < sizeof(TAIL_BYTES)) {
        return -1;
    }

    for (size_t i = 0; i + sizeof(TAIL_BYTES) <= len; ++i) {
        if (memcmp(&buf[i], TAIL_BYTES, sizeof(TAIL_BYTES)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint16_t read_be16(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static bool parse_temp_x10_ascii(const char *text, int16_t *out)
{
    if (!text || !out || *text == '\0') {
        return false;
    }

    char *end = NULL;
    long whole = strtol(text, &end, 10);
    int sign = (whole < 0) ? -1 : 1;
    int frac = 0;

    if (end && *end == '.') {
        ++end;
        if (!isdigit((unsigned char)*end)) {
            return false;
        }
        frac = *end - '0';
        ++end;
    }

    if (!end || *end != '\0') {
        return false;
    }

    long abs_whole = whole >= 0 ? whole : -whole;
    long value = abs_whole * 10L + frac;
    if (sign < 0) {
        value = -value;
    }
    if (value < INT16_MIN || value > INT16_MAX) {
        return false;
    }

    *out = (int16_t)value;
    return true;
}

static int channel_from_label_control(uint16_t page_id, uint16_t control_id)
{
    if (page_id != LCD_PAGE_MAIN) {
        return -1;
    }

    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        if (LABEL_IDS[i] == control_id) {
            return i;
        }
    }
    return -1;
}

static int channel_from_cfg_control(uint16_t page_id, uint16_t control_id,
                                    uint16_t base_id, uint16_t max_id)
{
    if ((page_id != LCD_PAGE_CFG_1_8 && page_id != LCD_PAGE_CFG_9_16) ||
        control_id < base_id || control_id > max_id) {
        return -1;
    }

    int page_base = (page_id == LCD_PAGE_CFG_1_8) ? 0 : 8;
    return page_base + (int)(control_id - base_id);
}

static void post_config_changed(void)
{
    esp_err_t err = esp_event_post(APP_EVENT, APP_EVENT_CONFIG_CHANGED, NULL, 0, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "post APP_EVENT_CONFIG_CHANGED failed: %s", esp_err_to_name(err));
    }
}

static void send_text(uint16_t page_id, uint16_t control_id, const char *text)
{
    uint8_t head[] = {
        FRAME_HEAD,
        0xB1,
        0x10,
        (uint8_t)(page_id >> 8),
        (uint8_t)page_id,
        (uint8_t)(control_id >> 8),
        (uint8_t)control_id,
    };
    uart_write_bytes(APP_UART_LCD_PORT, (const char *)head, sizeof(head));
    uart_write_bytes(APP_UART_LCD_PORT, text, strlen(text));
    uart_write_bytes(APP_UART_LCD_PORT, (const char *)TAIL_BYTES, sizeof(TAIL_BYTES));
}

static void send_binary(uint16_t page_id, uint16_t control_id, uint8_t value)
{
    uint8_t frame[] = {
        FRAME_HEAD,
        0xB1,
        0x10,
        (uint8_t)(page_id >> 8),
        (uint8_t)page_id,
        (uint8_t)(control_id >> 8),
        (uint8_t)control_id,
        value,
        TAIL_BYTES[0], TAIL_BYTES[1], TAIL_BYTES[2], TAIL_BYTES[3],
    };
    uart_write_bytes(APP_UART_LCD_PORT, (const char *)frame, sizeof(frame));
}

static void format_temp_x10(int16_t value, char *buf, size_t len)
{
    int16_t abs_value = value >= 0 ? value : (int16_t)(-value);
    if (value < 0 && abs_value < 10) {
        snprintf(buf, len, "-0.%d", abs_value % 10);
        return;
    }
    snprintf(buf, len, "%d.%d", value / 10, abs_value % 10);
}

static void push_alarm_fields(const channel_alarm_cfg_t *alarms)
{
    char buf[16];

    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        uint16_t page_id = (i < 8) ? LCD_PAGE_CFG_1_8 : LCD_PAGE_CFG_9_16;
        uint16_t page_offset = (uint16_t)(i % 8);

        send_binary(page_id, (uint16_t)(0x0001 + page_offset),
                    alarms[i].enabled == CH_STATUS_ENABLED ? 1 : 0);

        format_temp_x10(alarms[i].low_x10, buf, sizeof(buf));
        send_text(page_id, (uint16_t)(0x0011 + page_offset), buf);

        format_temp_x10(alarms[i].high_x10, buf, sizeof(buf));
        send_text(page_id, (uint16_t)(0x0019 + page_offset), buf);
    }
}

static void handle_rtc_frame(const uint8_t *frame, size_t len)
{
    if (len != 9) {
        ESP_LOGW(TAG, "unexpected RTC frame len=%u", (unsigned)len);
        return;
    }

    ESP_LOGI(TAG,
             "screen rtc sync sec=%02X min=%02X hour=%02X day=%02X week=%02X mon=%02X year=%02X",
             frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8]);
}

static void handle_b111_binary(uint16_t page_id, uint16_t control_id,
                               const uint8_t *payload, size_t payload_len)
{
    if (!s_alarms || payload_len != 3 || payload[0] != LCD_CTRL_TYPE_BINARY || payload[1] != 0x01) {
        return;
    }

    int channel = channel_from_cfg_control(page_id, control_id, 0x0001, 0x0008);
    if (channel < 0) {
        ESP_LOGI(TAG, "ignore binary ctrl page=0x%04X ctrl=0x%04X", page_id, control_id);
        return;
    }

    s_alarms[channel].enabled = payload[2] ? CH_STATUS_ENABLED : CH_STATUS_DISABLED;
    ESP_LOGI(TAG, "screen set ch%02d enabled=%d", channel + 1, s_alarms[channel].enabled);
    post_config_changed();
}

static void handle_b111_string(uint16_t page_id, uint16_t control_id,
                               const uint8_t *payload, size_t payload_len)
{
    if (payload_len < 2 || payload[0] != LCD_CTRL_TYPE_STRING || payload[payload_len - 1] != 0x00) {
        return;
    }

    char text[APP_LABEL_MAX_BYTES] = {0};
    size_t text_len = payload_len - 2;
    if (text_len >= sizeof(text)) {
        text_len = sizeof(text) - 1;
    }
    memcpy(text, &payload[1], text_len);

    int channel = channel_from_label_control(page_id, control_id);
    if (channel >= 0 && s_labels) {
        strlcpy(s_labels[channel].label, text, sizeof(s_labels[channel].label));
        ESP_LOGI(TAG, "screen set ch%02d label=%s", channel + 1, s_labels[channel].label);
        post_config_changed();
        return;
    }

    channel = channel_from_cfg_control(page_id, control_id, 0x0011, 0x0018);
    if (channel >= 0 && s_alarms) {
        int16_t low_x10 = 0;
        if (!parse_temp_x10_ascii(text, &low_x10)) {
            ESP_LOGW(TAG, "invalid low threshold text: %s", text);
            return;
        }
        s_alarms[channel].low_x10 = low_x10;
        ESP_LOGI(TAG, "screen set ch%02d low_x10=%d", channel + 1, low_x10);
        post_config_changed();
        return;
    }

    channel = channel_from_cfg_control(page_id, control_id, 0x0019, 0x0020);
    if (channel >= 0 && s_alarms) {
        int16_t high_x10 = 0;
        if (!parse_temp_x10_ascii(text, &high_x10)) {
            ESP_LOGW(TAG, "invalid high threshold text: %s", text);
            return;
        }
        s_alarms[channel].high_x10 = high_x10;
        ESP_LOGI(TAG, "screen set ch%02d high_x10=%d", channel + 1, high_x10);
        post_config_changed();
        return;
    }

    ESP_LOGI(TAG, "ignore string ctrl page=0x%04X ctrl=0x%04X text=%s", page_id, control_id, text);
}

static void handle_frame(const uint8_t *frame, size_t len)
{
    if (len < 2 || frame[0] != FRAME_HEAD) {
        return;
    }

    if (frame[1] == 0x81) {
        handle_rtc_frame(frame, len);
        return;
    }

    if (len < 3) {
        return;
    }

    uint16_t cmd = read_be16(&frame[1]);
    if (cmd != LCD_CMD_REPORT_CTRL || len < 8) {
        ESP_LOGI(TAG, "ignore frame cmd=0x%04X len=%u", cmd, (unsigned)len);
        return;
    }

    uint16_t page_id = read_be16(&frame[3]);
    uint16_t control_id = read_be16(&frame[5]);
    const uint8_t *payload = &frame[7];
    size_t payload_len = len - 7;

    if (payload_len == 0) {
        return;
    }

    if (payload[0] == LCD_CTRL_TYPE_BINARY) {
        handle_b111_binary(page_id, control_id, payload, payload_len);
        return;
    }
    if (payload[0] == LCD_CTRL_TYPE_STRING) {
        handle_b111_string(page_id, control_id, payload, payload_len);
        return;
    }

    ESP_LOGI(TAG, "ignore ctrl frame page=0x%04X ctrl=0x%04X type=0x%02X",
             page_id, control_id, payload[0]);
}

static void lcd_rx_task(void *arg)
{
    uint8_t rxbuf[256];
    uint8_t framebuf[512];
    size_t used = 0;

    while (1) {
        int n = uart_read_bytes(APP_UART_LCD_PORT, rxbuf, sizeof(rxbuf), pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }

        if (used + (size_t)n > sizeof(framebuf)) {
            ESP_LOGW(TAG, "rx buffer overflow, dropping %u bytes", (unsigned)(used + (size_t)n));
            used = 0;
        }

        memcpy(&framebuf[used], rxbuf, (size_t)n);
        used += (size_t)n;

        while (used > 0) {
            size_t head_pos = 0;
            while (head_pos < used && framebuf[head_pos] != FRAME_HEAD) {
                ++head_pos;
            }
            if (head_pos > 0) {
                memmove(framebuf, &framebuf[head_pos], used - head_pos);
                used -= head_pos;
            }
            if (used == 0) {
                break;
            }

            int tail_pos = find_tail(framebuf, used);
            if (tail_pos < 0) {
                break;
            }

            size_t frame_len = (size_t)tail_pos;
            handle_frame(framebuf, frame_len);

            size_t consume = frame_len + sizeof(TAIL_BYTES);
            memmove(framebuf, &framebuf[consume], used - consume);
            used -= consume;
        }
    }
}

void lcd_proto_bind_config(system_config_t *sys,
                           channel_label_t labels[APP_CHANNEL_COUNT],
                           channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    s_sys_cfg = sys;
    s_labels = labels;
    s_alarms = alarms;
}

esp_err_t lcd_proto_init(void)
{
    uart_config_t cfg = {
        .baud_rate = APP_UART_LCD_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_UART_LCD_PORT, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_LCD_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_LCD_PORT, BOARD_GPIO_UART_TX, BOARD_GPIO_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(lcd_rx_task, "lcd_rx", 4096, NULL, 4, NULL);
    return ESP_OK;
}

esp_err_t lcd_proto_push_temperatures(const temp_frame_t *frame)
{
    char buf[16];

    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        if (frame->channels[i].enabled == CH_STATUS_DISABLED) {
            snprintf(buf, sizeof(buf), "OFF");
        } else if (frame->channels[i].fault == TEMP_FAULT_NORMAL) {
            format_temp_x10(frame->channels[i].temp_x10, buf, sizeof(buf));
        } else {
            snprintf(buf, sizeof(buf), "EEEE");
        }
        send_text(LCD_PAGE_MAIN, TEMP_IDS[i], buf);
    }
    return ESP_OK;
}

esp_err_t lcd_proto_push_full_config(const channel_label_t *labels, const channel_alarm_cfg_t *alarms)
{
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        send_text(LCD_PAGE_MAIN, LABEL_IDS[i], labels[i].label);
    }
    push_alarm_fields(alarms);

    if (s_sys_cfg) {
        ESP_LOGI(TAG, "full config pushed to screen for device=%s", s_sys_cfg->device_name);
    }
    return ESP_OK;
}
