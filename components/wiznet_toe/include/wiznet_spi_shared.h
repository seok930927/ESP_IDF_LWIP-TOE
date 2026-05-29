#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared W5500 SPI access for a single physical chip driven by TWO subsystems:
 *   - esp_eth   : MACRAW on socket 0 (lwIP path)
 *   - ioLibrary : hardware TOE on sockets 1..7
 *
 * There is ONE chip, ONE SPI device and ONE CS line. Both subsystems must go
 * through the SAME spi_device handle so the ESP SPI master drives CS per
 * transaction and a shared recursive lock serializes access. This avoids the
 * old conflict where ioLibrary reconfigured the CS GPIO and broke esp_eth's
 * hardware CS.
 *
 * The handle is created lazily by esp_eth via wiznet_shared_spi_driver()
 * (injected as the W5500 custom SPI driver), so esp_eth must be initialized
 * before the ioLibrary SPI port registers its callbacks.
 */

/* esp_eth W5500 custom SPI driver. Plug into eth_w5500_config_t.custom_spi_driver
 * (and set its .config to the eth_w5500_config_t* so init can read host/devcfg). */
eth_spi_custom_driver_config_t wiznet_shared_spi_driver(void);

/* True once esp_eth has created the shared spi_device handle. */
bool wiznet_shared_spi_ready(void);

/* Recursive lock guarding every access to the shared W5500 SPI device. */
void wiznet_shared_spi_lock(void);
void wiznet_shared_spi_unlock(void);

/* Issue one W5500 SPI transaction on the shared device. The caller must hold
 * the lock (the ioLibrary callbacks do; the esp_eth read/write wrappers lock
 * internally).
 *   cmd  -> W5500 16-bit offset address (SPI command phase, command_bits=16)
 *   addr -> W5500 8-bit control byte    (SPI address phase, address_bits=8)
 */
esp_err_t wiznet_shared_spi_raw(bool is_read, uint32_t cmd, uint32_t addr,
                                void *data, uint32_t len);

#ifdef __cplusplus
}
#endif
