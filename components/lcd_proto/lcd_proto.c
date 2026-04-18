#include <stdlib.h>
#include "lcd_proto.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "board.h"
#include "app_config.h"

static const uint8_t TAIL_BYTES[] = {0xFF, 0xFC, 0xFF, 0xFF};
static const uint16_t TEMP_IDS[16] = {0x0014,0x0019,0x001E,0x0023,0x0028,0x002D,0x0032,0x0037,0x003C,0x0041,0x0046,0x004B,0x0050,0x0055,0x005A,0x005F};
static const uint16_t LABEL_IDS[16] = {0x0015,0x001A,0x001F,0x0024,0x0029,0x002E,0x0033,0x0038,0x003D,0x0042,0x0047,0x004C,0x0051,0x0056,0x005B,0x0060};

static void send_b1100(uint16_t page_id, uint16_t control_id, const char *text)
{
    uint8_t head[] = {0xEE, 0xB1, 0x10, (uint8_t)(page_id >> 8), (uint8_t)page_id,
                      (uint8_t)(control_id >> 8), (uint8_t)control_id};
    uart_write_bytes(APP_UART_LCD_PORT, (const char *)head, sizeof(head));
    uart_write_bytes(APP_UART_LCD_PORT, text, strlen(text));
    uart_write_bytes(APP_UART_LCD_PORT, (const char *)TAIL_BYTES, sizeof(TAIL_BYTES));
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
    ESP_ERROR_CHECK(uart_driver_install(APP_UART_LCD_PORT, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_LCD_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_LCD_PORT, BOARD_GPIO_UART_TX, BOARD_GPIO_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    return ESP_OK;
}

esp_err_t lcd_proto_push_temperatures(const temp_frame_t *frame)
{
    char buf[16];
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        if (frame->channels[i].fault == TEMP_FAULT_NORMAL) {
            snprintf(buf, sizeof(buf), "%d.%d", frame->channels[i].temp_x10 / 10, abs(frame->channels[i].temp_x10 % 10));
        } else {
            snprintf(buf, sizeof(buf), "EEEE");
        }
        send_b1100(0x0000, TEMP_IDS[i], buf);
    }
    return ESP_OK;
}

esp_err_t lcd_proto_push_full_config(const channel_label_t *labels, const channel_alarm_cfg_t *alarms)
{
    char buf[16];
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        send_b1100(0x0000, LABEL_IDS[i], labels[i].label);
        (void)alarms;
        snprintf(buf, sizeof(buf), "%d.%d", alarms[i].high_x10 / 10, abs(alarms[i].high_x10 % 10));
    }
    return ESP_OK;
}
