#pragma once
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

// Schematic net: LED3 / W_LED, active high.
#define BOARD_GPIO_LED_NET             GPIO_NUM_8
// Provision reset key, active low with internal pull-up.
#define BOARD_GPIO_KEY_PROV_RESET      GPIO_NUM_4

// Shared MAX31856 SPI bus
// Schematic net: MMOSI
#define BOARD_GPIO_SPI_MOSI            GPIO_NUM_7
// Schematic net: CCLK
#define BOARD_GPIO_SPI_CLK             GPIO_NUM_6
// Schematic net: MISO
#define BOARD_GPIO_SPI_MISO            GPIO_NUM_2

// MAX31856 DRDY / FAULT are not connected to ESP32-C3 on this board.
#define BOARD_GPIO_MAX31856_DRDY       GPIO_NUM_NC
#define BOARD_GPIO_MAX31856_FAULT      GPIO_NUM_NC

// 16-channel chip-select mux select lines, matching schematic CH_SEL1..4.
#define BOARD_GPIO_MAX31856_MUX_A0     GPIO_NUM_10
#define BOARD_GPIO_MAX31856_MUX_A1     GPIO_NUM_1
#define BOARD_GPIO_MAX31856_MUX_A2     GPIO_NUM_0
#define BOARD_GPIO_MAX31856_MUX_A3     GPIO_NUM_3
// Schematic net: CS_EN
#define BOARD_GPIO_MAX31856_MUX_EN     GPIO_NUM_5

#define BOARD_GPIO_UART_TX             GPIO_NUM_21
#define BOARD_GPIO_UART_RX             GPIO_NUM_20

#define BOARD_MAX31856_CHANNEL_COUNT   16

typedef enum {
    BOARD_STATUS_LED_OFF = 0,
    BOARD_STATUS_LED_UNPROVISIONED,
    BOARD_STATUS_LED_PROVISIONING,
    BOARD_STATUS_LED_ONLINE,
} board_status_led_mode_t;

esp_err_t board_init(void);
void board_set_status_led_mode(board_status_led_mode_t mode);
bool board_is_prov_reset_pressed(void);
bool board_gpio_is_valid(gpio_num_t gpio);
