#include "max31856.h"
#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include "board.h"
#include "cs_mux.h"

static const char *TAG = "max31856";
static spi_device_handle_t s_spi;

static esp_err_t spi_rw(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_spi, &t);
}

esp_err_t max31856_bus_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = BOARD_GPIO_SPI_MOSI,
        .miso_io_num = BOARD_GPIO_SPI_MISO,
        .sclk_io_num = BOARD_GPIO_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode = 1,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(cs_mux_init());
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi));
    ESP_LOGI(TAG, "SPI bus initialized");
    return ESP_OK;
}

esp_err_t max31856_init_channel(uint8_t channel, const max31856_cfg_t *cfg)
{
    (void)cfg;
    cs_mux_select(channel);
    // TODO: write CR0/CR1 registers according to final hardware validation.
    cs_mux_deselect_all();
    return ESP_OK;
}

esp_err_t max31856_read_channel(uint8_t channel, channel_runtime_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->enabled = CH_STATUS_ENABLED;
    cs_mux_select(channel);
    // Skeleton: fake read path. Replace with actual register read sequence.
    out->temp_x10 = 200 + channel;
    out->fault = TEMP_FAULT_NORMAL;
    cs_mux_deselect_all();
    return ESP_OK;
}
