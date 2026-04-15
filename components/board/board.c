#include "board.h"
#include "driver/gpio.h"

esp_err_t board_init(void)
{
    gpio_config_t io = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_GPIO_LED_PROV) | (1ULL << BOARD_GPIO_LED_NET),
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    board_set_prov_led(false);
    board_set_net_led(false);
    return ESP_OK;
}

void board_set_prov_led(bool on)
{
    gpio_set_level(BOARD_GPIO_LED_PROV, on ? 1 : 0);
}

void board_set_net_led(bool on)
{
    gpio_set_level(BOARD_GPIO_LED_NET, on ? 1 : 0);
}
