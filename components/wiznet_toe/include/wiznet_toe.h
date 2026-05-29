#pragma once

#include "esp_err.h"
#include "esp_netif.h"

typedef struct wiz_NetInfo_t wiz_NetInfo;

typedef void (*wiznet_toe_socket_event_cb_t)(uint8_t socket_no, uint8_t ir_bits, void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wiznet_toe_init(void);
esp_err_t wiznet_toe_partition_sockets(void);
esp_err_t wiznet_toe_get_netinfo(wiz_NetInfo *net_info);
esp_err_t wiznet_toe_set_netinfo(const wiz_NetInfo *net_info);
esp_netif_t *wiznet_toe_netif_create(const uint8_t mac[6]);
esp_err_t wiznet_toe_netif_sync_ip(esp_netif_t *toe_netif, const esp_netif_ip_info_t *ip_info);
esp_err_t wiznet_toe_apply_ip_to_chip(const esp_netif_ip_info_t *ip_info);
esp_err_t wiznet_toe_dispatcher_start(void);
esp_err_t wiznet_toe_dispatcher_stop(void);
esp_err_t wiznet_toe_register_socket_event_cb(wiznet_toe_socket_event_cb_t cb, void *ctx);

/* Optional TOE TCP echo server demo (CONFIG_WIZNET_TOE_ECHO_DEMO).
 * Starts a background task that listens on `port` using a hardware TOE socket
 * and echoes back received bytes. Used to exercise the TOE data path and to
 * observe whether the MACRAW/lwIP path (eth0) duplicates / RSTs TOE traffic
 * (problem #3). Safe to call once the chip has a valid IP (after GOT_IP). */
esp_err_t wiznet_toe_start_echo_server(uint16_t port);

#ifdef __cplusplus
}
#endif
