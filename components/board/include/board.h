#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define BOARD_GPIO_LED_PROV 4
#define BOARD_GPIO_LED_NET  8
#define BOARD_GPIO_SPI_MOSI 7
#define BOARD_GPIO_SPI_CLK  6
#define BOARD_GPIO_SPI_MISO -1
#define BOARD_GPIO_UART_TX  21
#define BOARD_GPIO_UART_RX  20

esp_err_t board_init(void);
void board_set_prov_led(bool on);
void board_set_net_led(bool on);
