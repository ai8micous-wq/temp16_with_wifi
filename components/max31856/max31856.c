#include "max31856.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "board.h"
#include "cs_mux.h"

static const char *TAG = "max31856";
static spi_device_handle_t s_spi;
static max31856_debug_snapshot_t s_debug_snapshots[APP_CHANNEL_COUNT];

enum {
    REG_CR0 = 0x00,
    REG_CR1 = 0x01,
    REG_MASK = 0x02,
    REG_LTCBH = 0x0C,
    REG_SR = 0x0F,
};

enum {
    CR0_AUTOCONVERT = 1 << 7,
    CR0_OCFAULT_10MS = 1 << 4,
    CR0_FILTER_50HZ = 1 << 0,
};

enum {
    SR_CJ_RANGE = 1 << 7,
    SR_TC_RANGE = 1 << 6,
    SR_CJ_HIGH = 1 << 5,
    SR_CJ_LOW = 1 << 4,
    SR_TC_HIGH = 1 << 3,
    SR_TC_LOW = 1 << 2,
    SR_OVUV = 1 << 1,
    SR_OPEN = 1 << 0,
};

static esp_err_t spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_spi, &t);
}

static esp_err_t max31856_write_reg(uint8_t channel, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80U), value };
    esp_err_t err = cs_mux_select(channel);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_transfer(tx, NULL, sizeof(tx));
    cs_mux_deselect_all();
    return err;
}

static esp_err_t max31856_read_regs(uint8_t channel, uint8_t reg, uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > 4) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[5] = {0};
    uint8_t rx[5] = {0};
    tx[0] = reg & 0x7FU;

    esp_err_t err = cs_mux_select(channel);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_transfer(tx, rx, len + 1);
    cs_mux_deselect_all();
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < len; ++i) {
        data[i] = rx[i + 1];
    }
    return ESP_OK;
}

static int16_t max31856_decode_temp_x10(const uint8_t raw_bytes[3])
{
    // Bits [23:5] are a signed 19-bit temperature with 1/128 C LSB.
    int32_t raw = ((int32_t)raw_bytes[0] << 16) | ((int32_t)raw_bytes[1] << 8) | raw_bytes[2];
    if (raw & 0x800000L) {
        raw |= ~0xFFFFFFL;
    }
    raw >>= 5;

    int32_t scaled = raw * 10;
    if (scaled >= 0) {
        scaled += 64;
    } else {
        scaled -= 64;
    }
    return (int16_t)(scaled / 128);
}

static temp_fault_t max31856_fault_from_status(uint8_t sr)
{
    if (sr & SR_OPEN) {
        return TEMP_FAULT_OPEN;
    }
    if (sr & (SR_OVUV | SR_CJ_RANGE | SR_CJ_HIGH | SR_CJ_LOW)) {
        return TEMP_FAULT_COMM;
    }
    if (sr & (SR_TC_HIGH | SR_TC_RANGE)) {
        return TEMP_FAULT_HIGH;
    }
    if (sr & SR_TC_LOW) {
        return TEMP_FAULT_LOW;
    }
    return TEMP_FAULT_NORMAL;
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
    ESP_LOGI(TAG, "SPI bus initialized%s",
             !board_gpio_is_valid(BOARD_GPIO_SPI_MISO)
                 ? " (warning: MISO disabled, reads require a valid BOARD_GPIO_SPI_MISO)"
                 : "");
    return ESP_OK;
}

esp_err_t max31856_init_channel(uint8_t channel, const max31856_cfg_t *cfg)
{
    if (!cfg || !s_spi) {
        return ESP_ERR_INVALID_STATE;
    }
    if (channel >= APP_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cr0 = CR0_OCFAULT_10MS;
    if (cfg->filter_50hz) {
        cr0 |= CR0_FILTER_50HZ;
    }

    uint8_t avg_sel = cfg->avg_sel > 4 ? 4 : cfg->avg_sel;
    uint8_t cr1 = (uint8_t)((avg_sel & 0x7U) << 4) | ((uint8_t)cfg->tc_type & 0x0FU);

    esp_err_t err = max31856_write_reg(channel, REG_CR0, cr0);
    if (err != ESP_OK) {
        return err;
    }
    err = max31856_write_reg(channel, REG_CR1, cr1);
    if (err != ESP_OK) {
        return err;
    }
    err = max31856_write_reg(channel, REG_MASK, 0xFF);
    if (err != ESP_OK) {
        return err;
    }
    err = max31856_write_reg(channel, REG_CR0, (uint8_t)(cr0 | CR0_AUTOCONVERT));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "channel %u configured: tc_type=%u avg_sel=%u filter=%s",
                 channel, (unsigned)cfg->tc_type, (unsigned)avg_sel, cfg->filter_50hz ? "50Hz" : "60Hz");
    }
    return err;
}

esp_err_t max31856_read_channel(uint8_t channel, channel_runtime_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_spi) {
        return ESP_ERR_INVALID_STATE;
    }

    out->enabled = CH_STATUS_ENABLED;
    out->temp_x10 = 0;
    out->fault = TEMP_FAULT_INVALID;

    uint8_t raw[3] = {0};
    esp_err_t err = max31856_read_regs(channel, REG_LTCBH, raw, sizeof(raw));
    if (err != ESP_OK) {
        out->fault = TEMP_FAULT_COMM;
        return err;
    }

    uint8_t sr = 0;
    err = max31856_read_regs(channel, REG_SR, &sr, 1);
    if (err != ESP_OK) {
        out->fault = TEMP_FAULT_COMM;
        return err;
    }

    s_debug_snapshots[channel].raw_temp[0] = raw[0];
    s_debug_snapshots[channel].raw_temp[1] = raw[1];
    s_debug_snapshots[channel].raw_temp[2] = raw[2];
    s_debug_snapshots[channel].raw_status = sr;

    out->temp_x10 = max31856_decode_temp_x10(raw);
    out->fault = max31856_fault_from_status(sr);
    return ESP_OK;
}

bool max31856_get_last_debug_snapshot(uint8_t channel, max31856_debug_snapshot_t *out)
{
    if (!out || channel >= APP_CHANNEL_COUNT) {
        return false;
    }
    *out = s_debug_snapshots[channel];
    return true;
}
