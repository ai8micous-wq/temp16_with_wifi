#include "cs_mux.h"
#include "board.h"
#include "driver/gpio.h"

static const gpio_num_t s_sel_pins[5] = {
    BOARD_GPIO_MAX31856_MUX_A0,
    BOARD_GPIO_MAX31856_MUX_A1,
    BOARD_GPIO_MAX31856_MUX_A2,
    BOARD_GPIO_MAX31856_MUX_A3,
    BOARD_GPIO_MAX31856_MUX_EN,
};

static void write_bits(uint8_t channel, bool disable_all)
{
    for (int i = 0; i < 4; ++i) {
        gpio_set_level(s_sel_pins[i], disable_all ? 0 : ((channel >> i) & 0x1));
    }
    gpio_set_level(s_sel_pins[4], disable_all ? 1 : 0);
}

esp_err_t cs_mux_init(void)
{
    gpio_config_t io = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_GPIO_MAX31856_MUX_A0) |
                        (1ULL << BOARD_GPIO_MAX31856_MUX_A1) |
                        (1ULL << BOARD_GPIO_MAX31856_MUX_A2) |
                        (1ULL << BOARD_GPIO_MAX31856_MUX_A3) |
                        (1ULL << BOARD_GPIO_MAX31856_MUX_EN),
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    return cs_mux_deselect_all();
}

esp_err_t cs_mux_select(uint8_t channel)
{
    if (channel > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    write_bits(channel, false);
    return ESP_OK;
}

esp_err_t cs_mux_deselect_all(void)
{
    write_bits(0, true);
    return ESP_OK;
}
