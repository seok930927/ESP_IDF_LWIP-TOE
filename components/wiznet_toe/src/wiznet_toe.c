#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "wizchip_conf.h"
#include "wiznet_toe.h"
#include "wiznet_toe_port.h"

static const char *TAG = "wiznet_toe";
static bool s_initialized;

esp_err_t wiznet_toe_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wiznet_spi_port_init(), TAG, "SPI port init failed");

    /*
     * IMPORTANT (single shared W5500):
     * Do NOT call wizchip_init() here. wizchip_init() -> wizchip_sw_reset()
     * issues a full-chip software reset (setMR(MR_RST)) that would erase the
     * MACRAW mode, MAC address, PHY and socket-0 buffer setup that esp_eth has
     * already applied to the SAME physical chip, silently killing eth0 (no DHCP,
     * no IP). esp_eth is the sole owner of chip reset and socket-0 (MACRAW) setup.
     *
     * The SPI access callbacks (cris/cs/spi/spiburst) needed by ioLibrary are
     * registered in wiznet_spi_port_init(), so no further chip-level init is
     * required just to drive sockets 1..7.
     */

    /*
     * Problem #2 (buffer partition): esp_eth gives socket-0 the whole 16KB
     * (MACRAW) and 0KB to sockets 1..7, so TOE sockets have no buffer. Re-split
     * the 16KB TX / 16KB RX evenly (2KB x 8) so sockets 1..7 are usable.
     * NOTE: for this to be race-free it must run before esp_eth issues the
     * socket-0 OPEN command (i.e. wiznet_toe_init() must be called between
     * example_eth_init() and esp_eth_start()). See SPI/CS sharing note.
     */
    ESP_RETURN_ON_ERROR(wiznet_toe_partition_sockets(), TAG, "socket buffer partition failed");

    wiz_NetTimeout timeout = {
        .retry_cnt = 8,
        .time_100us = 2000,
    };
    if (ctlnetwork(CN_SET_TIMEOUT, &timeout) != 0) {
        ESP_LOGE(TAG, "failed to set WIZnet timeout");
        return ESP_FAIL;
    }

#if CONFIG_WIZNET_TOE_DISPATCHER_ENABLE
    ESP_RETURN_ON_ERROR(wiznet_toe_dispatcher_start(), TAG, "dispatcher start failed");
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "ioLibrary initialized for W5500");
    return ESP_OK;
}

esp_err_t wiznet_toe_partition_sockets(void)
{
    /*
     * Single shared W5500: split 16KB TX / 16KB RX evenly across all 8 sockets
     * (2KB each). Socket 0 (MACRAW / eth0) keeps 2KB, which is enough for the
     * light control-plane traffic it carries (ARP / DHCP / ICMP); bulk data is
     * expected to flow through the hardware-offloaded TOE sockets 1..7.
     *
     * Sn_RXBUF_SIZE / Sn_TXBUF_SIZE take a value in KB (power of two). The sum
     * of all RX sizes (and all TX sizes) must not exceed 16KB; 2KB x 8 == 16KB.
     *
     * Must be written while the affected sockets are CLOSED (before OPEN), so
     * the chip's per-socket buffer base addresses are computed once and stay
     * consistent for both the MACRAW (esp_eth) and TOE (ioLibrary) views.
     */
    for (uint8_t sn = 0; sn < _WIZCHIP_SOCK_NUM_; sn++) {
        setSn_RXBUF_SIZE(sn, 2);
        setSn_TXBUF_SIZE(sn, 2);
    }
    ESP_LOGI(TAG, "W5500 socket buffers partitioned: 2KB x %d (TX/RX)", _WIZCHIP_SOCK_NUM_);
    return ESP_OK;
}

esp_err_t wiznet_toe_get_netinfo(wiz_NetInfo *net_info)
{
    ESP_RETURN_ON_FALSE(net_info != NULL, ESP_ERR_INVALID_ARG, TAG, "net_info is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "ioLibrary not initialized");

    if (ctlnetwork(CN_GET_NETINFO, net_info) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wiznet_toe_set_netinfo(const wiz_NetInfo *net_info)
{
    ESP_RETURN_ON_FALSE(net_info != NULL, ESP_ERR_INVALID_ARG, TAG, "net_info is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "ioLibrary not initialized");

    wiz_NetInfo local_cfg;
    memcpy(&local_cfg, net_info, sizeof(local_cfg));
    if (ctlnetwork(CN_SET_NETINFO, &local_cfg) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
