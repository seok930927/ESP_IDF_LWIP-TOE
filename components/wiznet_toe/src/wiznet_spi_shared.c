#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#include "wiznet_spi_shared.h"

static const char *TAG = "wiznet_spi_shared";

static spi_device_handle_t s_hdl;
static SemaphoreHandle_t s_lock;   /* recursive: nests cris -> cs -> raw */

/* ---- esp_eth W5500 custom SPI driver callbacks ---------------------------- */

static void *shared_spi_init(const void *config)
{
    /* esp_eth passes eth_w5500_config_t* here (we set .config to it). */
    const eth_w5500_config_t *w5500 = (const eth_w5500_config_t *)config;
    if (w5500 == NULL || w5500->spi_devcfg == NULL) {
        ESP_LOGE(TAG, "missing W5500 spi config");
        return NULL;
    }

    spi_device_interface_config_t devcfg = *(w5500->spi_devcfg);
    /* W5500 SPI frame: command_bits=16 (offset address), address_bits=8 (control). */
    if (devcfg.command_bits == 0 && devcfg.address_bits == 0) {
        devcfg.command_bits = 16;
        devcfg.address_bits = 8;
    }

    if (spi_bus_add_device(w5500->spi_host_id, &devcfg, &s_hdl) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return NULL;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateRecursiveMutex();
        if (s_lock == NULL) {
            spi_bus_remove_device(s_hdl);
            s_hdl = NULL;
            return NULL;
        }
    }

    ESP_LOGI(TAG, "shared W5500 SPI device created (host=%d)", w5500->spi_host_id);
    /* Non-NULL ctx; single chip so we key off the file-scope globals. */
    return (void *)&s_hdl;
}

static esp_err_t shared_spi_deinit(void *ctx)
{
    (void)ctx;
    if (s_hdl != NULL) {
        spi_bus_remove_device(s_hdl);
        s_hdl = NULL;
    }
    if (s_lock != NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    return ESP_OK;
}

static esp_err_t shared_spi_read(void *ctx, uint32_t cmd, uint32_t addr, void *data, uint32_t len)
{
    (void)ctx;
    wiznet_shared_spi_lock();
    esp_err_t ret = wiznet_shared_spi_raw(true, cmd, addr, data, len);
    wiznet_shared_spi_unlock();
    return ret;
}

static esp_err_t shared_spi_write(void *ctx, uint32_t cmd, uint32_t addr, const void *data, uint32_t len)
{
    (void)ctx;
    wiznet_shared_spi_lock();
    esp_err_t ret = wiznet_shared_spi_raw(false, cmd, addr, (void *)data, len);
    wiznet_shared_spi_unlock();
    return ret;
}

eth_spi_custom_driver_config_t wiznet_shared_spi_driver(void)
{
    eth_spi_custom_driver_config_t cfg = {
        .config = NULL,            /* caller sets this to the eth_w5500_config_t* */
        .init   = shared_spi_init,
        .deinit = shared_spi_deinit,
        .read   = shared_spi_read,
        .write  = shared_spi_write,
    };
    return cfg;
}

/* ---- shared primitives used by both esp_eth and the ioLibrary SPI port ----- */

bool wiznet_shared_spi_ready(void)
{
    return s_hdl != NULL;
}

void wiznet_shared_spi_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    }
}

void wiznet_shared_spi_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGiveRecursive(s_lock);
    }
}

esp_err_t wiznet_shared_spi_raw(bool is_read, uint32_t cmd, uint32_t addr, void *data, uint32_t len)
{
    if (s_hdl == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t trans = {
        .cmd = (uint16_t)cmd,
        .addr = addr,
        .length = 8 * len,
    };

    if (is_read) {
        /* Small register reads use the in-transaction rx_data to avoid 4-byte
         * boundary overwrites on the caller's buffer (mirrors esp_eth). */
        trans.flags = (len <= 4) ? SPI_TRANS_USE_RXDATA : 0;
        trans.rx_buffer = data;
    } else {
        trans.tx_buffer = data;
    }

    esp_err_t ret = spi_device_polling_transmit(s_hdl, &trans);
    if (ret == ESP_OK && is_read && (trans.flags & SPI_TRANS_USE_RXDATA)) {
        memcpy(data, trans.rx_data, len);
    }
    return ret;
}
