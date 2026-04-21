#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile board_status_led_mode_t s_led_mode = BOARD_STATUS_LED_OFF;
static bool s_led_level;

bool board_gpio_is_valid(gpio_num_t gpio)
{
    return gpio >= 0 && gpio != GPIO_NUM_NC;
}

static void status_led_task(void *arg)
{
    (void)arg;

    while (1) {
        TickType_t delay_ticks = pdMS_TO_TICKS(1000);

        switch (s_led_mode) {
        case BOARD_STATUS_LED_OFF:
            s_led_level = false;
            gpio_set_level(BOARD_GPIO_LED_NET, 0);
            delay_ticks = pdMS_TO_TICKS(200);
            break;
        case BOARD_STATUS_LED_UNPROVISIONED:
            s_led_level = !s_led_level;
            gpio_set_level(BOARD_GPIO_LED_NET, s_led_level ? 1 : 0);
            delay_ticks = pdMS_TO_TICKS(1000);
            break;
        case BOARD_STATUS_LED_PROVISIONING:
            s_led_level = !s_led_level;
            gpio_set_level(BOARD_GPIO_LED_NET, s_led_level ? 1 : 0);
            delay_ticks = pdMS_TO_TICKS(300);
            break;
        case BOARD_STATUS_LED_ONLINE:
            s_led_level = true;
            gpio_set_level(BOARD_GPIO_LED_NET, 1);
            delay_ticks = pdMS_TO_TICKS(200);
            break;
        default:
            s_led_level = false;
            gpio_set_level(BOARD_GPIO_LED_NET, 0);
            delay_ticks = pdMS_TO_TICKS(200);
            break;
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t board_init(void)
{
    gpio_config_t led_io = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_GPIO_LED_NET),
    };
    ESP_ERROR_CHECK(gpio_config(&led_io));

    gpio_config_t key_io = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_GPIO_KEY_PROV_RESET),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&key_io));

    uint64_t max31856_input_mask = 0;
    gpio_num_t max31856_drdy = BOARD_GPIO_MAX31856_DRDY;
    gpio_num_t max31856_fault = BOARD_GPIO_MAX31856_FAULT;
    if (board_gpio_is_valid(max31856_drdy)) {
        max31856_input_mask |= (1ULL << max31856_drdy);
    }
    if (board_gpio_is_valid(max31856_fault)) {
        max31856_input_mask |= (1ULL << max31856_fault);
    }
    if (max31856_input_mask != 0) {
        gpio_config_t input_io = {
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = max31856_input_mask,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&input_io));
    }

    s_led_mode = BOARD_STATUS_LED_OFF;
    s_led_level = false;
    gpio_set_level(BOARD_GPIO_LED_NET, 0);
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 2, NULL);
    return ESP_OK;
}

void board_set_status_led_mode(board_status_led_mode_t mode)
{
    s_led_mode = mode;
}

bool board_is_prov_reset_pressed(void)
{
    return gpio_get_level(BOARD_GPIO_KEY_PROV_RESET) == 0;
}
