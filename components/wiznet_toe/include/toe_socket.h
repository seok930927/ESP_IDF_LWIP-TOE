#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal TOE socket layer over the W5500 hardware TCP/IP engine (ioLibrary).
 *
 * This is intentionally a thin, integer-handle API (the handle IS the W5500
 * hardware socket number, 1..7) rather than a full BSD/VFS shim. It is the
 * foundation the eventual socket() shim (plan Phase 4) will build on, and it
 * is enough to exercise the TOE data path end-to-end today.
 *
 * IMPORTANT: keep all ioLibrary usage inside the wiznet_toe component. The
 * ioLibrary `socket.h` redefines `connect`, `send`, `recv`, `close`, `listen`
 * as functions/macros that collide with lwIP/POSIX names, so files that talk
 * BSD sockets must NOT include this together with lwip/sockets.h.
 */

typedef enum {
    TOE_SOCK_TCP,
    TOE_SOCK_UDP,
} toe_sock_proto_t;

/* Allocate + open a hardware socket (1..7). Returns the socket number, or a
 * negative value if no slot is free / open failed. `local_port` may be 0 for
 * an ephemeral client port. */
int toe_sock_open(toe_sock_proto_t proto, uint16_t local_port);

/* Open a SPECIFIC hardware socket number (1..7), e.g. for a fixed server pool.
 * Returns sn on success or a negative value. */
int toe_sock_open_n(int sn, toe_sock_proto_t proto, uint16_t local_port);

/* Bytes currently available in the socket RX buffer (Sn_RX_RSR). Lets a single
 * task poll several sockets without blocking in recv. */
int toe_sock_rx_available(int sn);

/* TCP client: initiate a connection to ip ("a.b.c.d"):port. Non-blocking;
 * follow with toe_sock_wait_connected(). */
esp_err_t toe_sock_connect(int sn, const char *ip, uint16_t port);

/* TCP server: move the socket to LISTEN. */
esp_err_t toe_sock_listen(int sn);

/* Block until the TCP socket reaches ESTABLISHED. timeout_ms < 0 waits forever.
 * Returns ESP_OK, ESP_ERR_TIMEOUT, or ESP_FAIL (socket closed/refused). */
esp_err_t toe_sock_wait_connected(int sn, int timeout_ms);

/* TCP send-all. Returns bytes sent (== len) or a negative ioLibrary error. */
int toe_sock_send(int sn, const void *buf, size_t len);

/* TCP receive. Blocks until >= 1 byte is available; returns the number of
 * bytes read, 0 on orderly peer close, or a negative ioLibrary error. */
int toe_sock_recv(int sn, void *buf, size_t len);

/* 1 if the socket is in SOCK_ESTABLISHED (still connected, peer not FIN'd),
 * else 0. Lets the BSD shim implement blocking recv semantics without pulling
 * ioLibrary status constants out of the component. */
int toe_sock_connected(int sn);

/* Raw W5500 Sn_SR status byte (SOCK_ESTABLISHED, SOCK_LISTEN, ...). */
uint8_t toe_sock_status(int sn);

/* Disconnect (TCP) + close and free the slot. Safe on any state. */
void toe_sock_close(int sn);

#ifdef __cplusplus
}
#endif
