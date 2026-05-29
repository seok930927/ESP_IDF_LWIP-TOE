#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"

#include "wizchip_conf.h"
#include "wiznet_toe_port.h"
#include "wiznet_spi_shared.h"

static const char *TAG = "wiznet_spi_port";

/*
 * ioLibrary SPI port for the SHARED single W5500 device.
 *
 * ioLibrary's w5500.c drives every register/buffer access as a byte/burst
 * stream bracketed by CS select/deselect:
 *     CS._select();
 *     _write_burst(header, 3);          // [addr_hi][addr_lo][control]
 *     _read_byte() | _read_burst(buf,n) | _write_burst(buf,n) | (header+data)
 *     CS._deselect();
 *
 * We do NOT own a device or toggle CS here anymore. Instead we coalesce that
 * stream into the SAME single esp_eth-style transaction the shared driver
 * uses: the 3 header bytes become the SPI command (16-bit address) + address
 * (8-bit control) phases, and the read/write data is the payload. The shared
 * recursive lock (taken in cs_select / cris_enter) makes each access atomic
 * and serializes against the esp_eth MACRAW path.
 */

/* Per-access header state, valid only while the shared lock is held
 * (cs_select .. cs_deselect). Single chip => file-scope is fine. */
static uint8_t  s_hdr_len;
static uint32_t s_cmd;    /* 16-bit W5500 offset address */
static uint32_t s_ctrl;   /* 8-bit W5500 control byte (BSB | RWB | OM) */

static inline void hdr_reset(void)
{
    s_hdr_len = 0;
    s_cmd = 0;
    s_ctrl = 0;
}

static inline void hdr_feed(uint8_t b)
{
    if (s_hdr_len < 3) {
        if (s_hdr_len == 0) {
            s_cmd = (uint32_t)b << 8;          /* addr high */
        } else if (s_hdr_len == 1) {
            s_cmd |= (uint32_t)b;              /* addr low  */
        } else {
            s_ctrl = b;                        /* control byte */
        }
        s_hdr_len++;
    }
}

/* ioLibrary critical section -> shared recursive lock (multi-access atomic). */
static void cb_cris_enter(void) { wiznet_shared_spi_lock(); }
static void cb_cris_exit(void)  { wiznet_shared_spi_unlock(); }

/* CS select/deselect -> per-access atomic window; no GPIO toggling (HW CS). */
static void cb_cs_select(void)
{
    wiznet_shared_spi_lock();
    hdr_reset();
}

static void cb_cs_deselect(void)
{
    wiznet_shared_spi_unlock();
}

static uint8_t cb_readbyte(void)
{
    uint8_t v = 0;
    /* header (3 bytes) already fed via the preceding write(s) */
    wiznet_shared_spi_raw(true, s_cmd, s_ctrl, &v, 1);
    return v;
}

static void cb_writebyte(uint8_t b)
{
    if (s_hdr_len < 3) {
        hdr_feed(b);
    } else {
        wiznet_shared_spi_raw(false, s_cmd, s_ctrl, &b, 1);
    }
}

static void cb_readburst(uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    wiznet_shared_spi_raw(true, s_cmd, s_ctrl, buf, len);
}

static void cb_writeburst(uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (s_hdr_len < 3) {
        /* First burst after CS select carries the 3-byte header, possibly
         * followed by data in the same burst (register write == 4 bytes). */
        uint16_t take = (uint16_t)(3 - s_hdr_len);
        if (take > len) {
            take = len;
        }
        for (uint16_t i = 0; i < take; i++) {
            hdr_feed(buf[i]);
        }
        uint16_t remain = (uint16_t)(len - take);
        if (remain > 0 && s_hdr_len == 3) {
            wiznet_shared_spi_raw(false, s_cmd, s_ctrl, buf + take, remain);
        }
    } else {
        /* Subsequent burst == payload (buffer write). */
        wiznet_shared_spi_raw(false, s_cmd, s_ctrl, buf, len);
    }
}

esp_err_t wiznet_spi_port_init(void)
{
    ESP_RETURN_ON_FALSE(wiznet_shared_spi_ready(), ESP_ERR_INVALID_STATE, TAG,
                        "shared W5500 SPI device not ready (esp_eth must init first)");

    reg_wizchip_cris_cbfunc(cb_cris_enter, cb_cris_exit);
    reg_wizchip_cs_cbfunc(cb_cs_select, cb_cs_deselect);
    reg_wizchip_spi_cbfunc(cb_readbyte, cb_writebyte);
    reg_wizchip_spiburst_cbfunc(cb_readburst, cb_writeburst);

    ESP_LOGI(TAG, "ioLibrary SPI port bound to shared W5500 device");
    return ESP_OK;
}
