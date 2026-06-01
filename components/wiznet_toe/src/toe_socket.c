#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "socket.h"        /* ioLibrary: socket/connect/send/recv/close/listen */
#include "wizchip_conf.h"

#include "toe_socket.h"

static const char *TAG = "toe_sock";

/* Bitmap of W5500 sockets currently allocated to the TOE layer. Socket 0 is
 * reserved for esp_eth/MACRAW and is never handed out here. */
static uint8_t s_used_mask;

static inline bool slot_valid(int sn)  { return sn >= 1 && sn < _WIZCHIP_SOCK_NUM_; }
static inline bool slot_used(int sn)    { return (s_used_mask >> sn) & 0x1u; }
static inline void slot_take(int sn)    { s_used_mask |= (uint8_t)(1u << sn); }
static inline void slot_free(int sn)    { s_used_mask &= (uint8_t)~(1u << sn); }

static int alloc_slot(void)
{
    for (int sn = 1; sn < _WIZCHIP_SOCK_NUM_; sn++) {
        if (!slot_used(sn)) {
            return sn;
        }
    }
    return -1;
}

static esp_err_t parse_ipv4(const char *ip, uint8_t out[4])
{
    unsigned a, b, c, d;
    if (ip == NULL || sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return ESP_ERR_INVALID_ARG;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return ESP_OK;
}

int toe_sock_open_n(int sn, toe_sock_proto_t proto, uint16_t local_port)
{
    if (!slot_valid(sn)) {
        ESP_LOGE(TAG, "invalid TOE socket number %d", sn);
        return -1;
    }

    uint8_t mr = (proto == TOE_SOCK_TCP) ? Sn_MR_TCP : Sn_MR_UDP;
    /* Non-block mode so send/recv/connect return immediately and this layer
     * owns the yielding/polling cadence instead of busy-spinning the chip. */
    int8_t rc = socket((uint8_t)sn, mr, local_port, SF_IO_NONBLOCK);
    if (rc != (int8_t)sn) {
        ESP_LOGE(TAG, "socket(%d) failed: rc=%d", sn, rc);
        return -1;
    }

    slot_take(sn);
    return sn;
}

int toe_sock_open(toe_sock_proto_t proto, uint16_t local_port)
{
    int sn = alloc_slot();
    if (sn < 0) {
        ESP_LOGE(TAG, "no free TOE socket (1..%d all in use)", _WIZCHIP_SOCK_NUM_ - 1);
        return -1;
    }
    int rc = toe_sock_open_n(sn, proto, local_port);
    if (rc >= 0) {
        ESP_LOGI(TAG, "TOE socket %d opened (%s, local_port=%u)",
                 sn, (proto == TOE_SOCK_TCP) ? "TCP" : "UDP", local_port);
    }
    return rc;
}

int toe_sock_rx_available(int sn)
{
    if (!slot_valid(sn)) {
        return 0;
    }
    return (int)getSn_RX_RSR((uint8_t)sn);
}

esp_err_t toe_sock_connect(int sn, const char *ip, uint16_t port)
{
    if (!slot_valid(sn)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t addr[4];
    esp_err_t perr = parse_ipv4(ip, addr);
    if (perr != ESP_OK) {
        return perr;
    }

    /* In non-block mode connect() returns SOCK_BUSY once the SYN is issued; the
     * handshake completes asynchronously (poll via toe_sock_wait_connected). */
    int8_t rc = connect((uint8_t)sn, addr, port);
    if (rc == SOCK_OK || rc == SOCK_BUSY) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "connect(%d) to %s:%u failed: rc=%d", sn, ip, port, rc);
    return ESP_FAIL;
}

esp_err_t toe_sock_listen(int sn)
{
    if (!slot_valid(sn)) {
        return ESP_ERR_INVALID_ARG;
    }
    int8_t rc = listen((uint8_t)sn);
    if (rc != SOCK_OK) {
        ESP_LOGE(TAG, "listen(%d) failed: rc=%d", sn, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t toe_sock_wait_connected(int sn, int timeout_ms)
{
    if (!slot_valid(sn)) {
        return ESP_ERR_INVALID_ARG;
    }
    int waited = 0;
    const int step = 10;
    for (;;) {
        uint8_t sr = getSn_SR((uint8_t)sn);
        if (sr == SOCK_ESTABLISHED) {
            return ESP_OK;
        }
        if (sr == SOCK_CLOSED) {
            return ESP_FAIL;
        }
        if (timeout_ms >= 0 && waited >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        waited += step;
    }
}

int toe_sock_send(int sn, const void *buf, size_t len)
{
    if (!slot_valid(sn) || buf == NULL) {
        return -1;
    }
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        uint8_t sr = getSn_SR((uint8_t)sn);
        if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) {
            ESP_LOGW(TAG, "send(%d): socket not connected (SR=0x%02x)", sn, sr);
            return -1;
        }
        size_t chunk = len - sent;
        if (chunk > 0xFFFF) {
            chunk = 0xFFFF;
        }
        int32_t rc = send((uint8_t)sn, (uint8_t *)(p + sent), (uint16_t)chunk);
        if (rc == SOCK_BUSY) {            /* TX buffer full; let the wire drain */
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (rc < 0) {
            ESP_LOGE(TAG, "send(%d) failed: rc=%ld", sn, (long)rc);
            return (int)rc;
        }
        sent += (size_t)rc;
    }
    return (int)sent;
}

int toe_sock_recv(int sn, void *buf, size_t len)
{
    if (!slot_valid(sn) || buf == NULL || len == 0) {
        return -1;
    }

    uint8_t sr = getSn_SR((uint8_t)sn);
    if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) {
        return 0;                          /* not connected / peer closed */
    }

    uint16_t rsr = getSn_RX_RSR((uint8_t)sn);
    if (rsr == 0) {
        return 0;                          /* no buffered data (caller checks first) */
    }

    /*
     * Read the data ourselves instead of ioLibrary recv(): ioLibrary recv()
     * ends with `while (getSn_CR(sn));` -- an UNBOUNDED, no-yield busy-wait that
     * hangs the task (and starves the CPU) if the RECV command's completion read
     * is delayed by SPI contention with the esp_eth/MACRAW path. Here we issue
     * RECV and wait with a bounded, yielding loop so the task never gets stuck.
     */
    uint16_t want = (len > 0xFFFF) ? 0xFFFF : (uint16_t)len;
    uint16_t rd = (rsr < want) ? rsr : want;

    wiz_recv_data((uint8_t)sn, (uint8_t *)buf, rd);   /* copy out + advance Sn_RX_RD */
    setSn_CR((uint8_t)sn, Sn_CR_RECV);                /* commit the read pointer */

    for (int i = 0; i < 50; i++) {
        if (getSn_CR((uint8_t)sn) == 0) {
            break;                          /* command accepted (normal, ~instant) */
        }
        vTaskDelay(1);                      /* bounded yield (~1ms/tick @1000Hz) */
    }

    return (int)rd;
}

uint8_t toe_sock_status(int sn)
{
    if (!slot_valid(sn)) {
        return SOCK_CLOSED;
    }
    return getSn_SR((uint8_t)sn);
}

void toe_sock_close(int sn)
{
    if (!slot_valid(sn)) {
        return;
    }
    uint8_t sr = getSn_SR((uint8_t)sn);
    if (sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT) {
        disconnect((uint8_t)sn);
    }
    close((uint8_t)sn);
    slot_free(sn);
}
