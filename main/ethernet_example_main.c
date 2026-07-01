/* Ethernet Basic Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "ethernet_init.h"
#include "sdkconfig.h"
#if CONFIG_WIZNET_TOE_ENABLE
#include "wiznet_toe.h"
#include "toe_bsd.h"
#include "lwip/sockets.h"
#endif
#if CONFIG_WIZNET_TOE_ECHO_DEMO
#include "esp_rom_sys.h"
#include "lwip/sockets.h"
#endif

static const char *TAG = "eth_example";
#if CONFIG_WIZNET_TOE_ENABLE
static esp_netif_t *s_toe_netif;
#endif

#if CONFIG_WIZNET_TOE_ECHO_DEMO
#define LWIP_ECHO_PORT 5000   /* MACRAW/lwIP echo (TOE echo is on CONFIG_WIZNET_TOE_ECHO_PORT) */

/* esp_eth RX hook: logs every frame the MACRAW socket-0 delivers to lwIP, then
 * forwards it to the stack exactly like the default netif glue. Used to observe
 * problem #3: if TOE (sockets 1..7) traffic ALSO shows up here, MACRAW is
 * duplicating it into lwIP. Runs in the esp_eth task context (not ISR). */
static esp_err_t macraw_rx_input(esp_eth_handle_t eth_handle, uint8_t *buffer,
                                 uint32_t length, void *priv)
{
    (void)eth_handle;
    esp_rom_printf("MACRAW recv\r\n");
    return esp_netif_receive((esp_netif_t *)priv, buffer, length, NULL);
}

/* Standard BSD/lwIP TCP echo server. Rides the MACRAW path (socket 0 -> lwIP). */
static void lwip_echo_task(void *arg)
{
    (void)arg;
    char buf[1024];
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        ESP_LOGE(TAG, "lwip echo: socket() failed");
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(LWIP_ECHO_PORT),
    };
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 2) != 0) {
        ESP_LOGE(TAG, "lwip echo: bind/listen failed");
        close(ls);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "lwIP (MACRAW) echo server listening on :%d", LWIP_ECHO_PORT);

    for (;;) {
        int cs = accept(ls, NULL, NULL);
        if (cs < 0) {
            continue;
        }
        ESP_LOGI(TAG, "lwIP echo: client connected");
        for (;;) {
            int n = recv(cs, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            int off = 0;
            while (off < n) {
                int w = send(cs, buf + off, n - off, 0);
                if (w <= 0) { off = -1; break; }
                off += w;
            }
            if (off < 0) {
                break;
            }
        }
        ESP_LOGI(TAG, "lwIP echo: client closed");
        close(cs);
    }
}
#endif

#if CONFIG_WIZNET_TOE_BSD_CLIENT_DEMO
/* BSD shim client demo: open via wiz_socket (TCP -> TOE), connect to a server,
 * send/recv. Proves the shim routes standard-style BSD calls through the TOE
 * hardware path end-to-end. */
static void bsd_client_demo_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int n = 0; ; n++) {
        int fd = wiz_socket(AF_INET, SOCK_STREAM, 0);   /* TCP -> routed to TOE */
        struct sockaddr_in dst = { .sin_family = AF_INET };
        dst.sin_port = htons(CONFIG_WIZNET_TOE_BSD_SERVER_PORT);
        dst.sin_addr.s_addr = inet_addr(CONFIG_WIZNET_TOE_BSD_SERVER_IP);
        ESP_LOGI(TAG, "[bsd-client] connect %s:%d via %s (fd=%d)",
                 CONFIG_WIZNET_TOE_BSD_SERVER_IP, CONFIG_WIZNET_TOE_BSD_SERVER_PORT,
                 wiz_fd_is_toe(fd) ? "TOE" : "lwIP", fd);
        if (wiz_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            ESP_LOGW(TAG, "[bsd-client] connect failed");
            wiz_close(fd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        char msg[48];
        int mlen = snprintf(msg, sizeof(msg), "hello-via-shim-%d", n);
        wiz_send(fd, msg, mlen, 0);
        char rx[64];
        int r = wiz_recv(fd, rx, sizeof(rx) - 1, 0);
        if (r > 0) {
            rx[r] = '\0';
            ESP_LOGI(TAG, "[bsd-client] echo back: '%s' (%d B)", rx, r);
        } else {
            ESP_LOGW(TAG, "[bsd-client] recv=%d", r);
        }
        wiz_close(fd);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
#endif

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

#if CONFIG_WIZNET_TOE_ENABLE
    if (s_toe_netif != NULL) {
        ESP_ERROR_CHECK(wiznet_toe_netif_sync_ip(s_toe_netif, ip_info));
        ESP_ERROR_CHECK(wiznet_toe_apply_ip_to_chip(ip_info));
        ESP_LOGI(TAG, "toe0 synced to eth0 DHCP address");
#if CONFIG_WIZNET_TOE_ECHO_DEMO
        /* Chip now has a valid IP -> the TOE TCP engine can listen/connect. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(wiznet_toe_start_echo_server(0));
#endif
#if CONFIG_WIZNET_TOE_BSD_SERVER_DEMO
        /* Same echo, but driven through the BSD shim (build-level A/B test). */
        ESP_ERROR_CHECK_WITHOUT_ABORT(wiznet_toe_start_bsd_echo(0));
#endif
#if CONFIG_WIZNET_TOE_BSD_CLIENT_DEMO
        static bool bsd_client_started = false;
        if (!bsd_client_started) {
            bsd_client_started = true;
            xTaskCreate(bsd_client_demo_task, "bsd_client", 4096, NULL, 5, NULL);
        }
#endif
    }
#endif
}

void app_main(void)
{
    // Initialize Ethernet driver
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

#if CONFIG_WIZNET_TOE_ENABLE
    /* Bring up the ioLibrary TOE path now: esp_eth has created the shared SPI
     * device, and the W5500 socket-0 (MACRAW) is configured but NOT yet OPEN
     * (that happens in esp_eth_start below). Doing it here lets us re-partition
     * the socket buffers (2KB x 8, problem #2) race-free before any socket is
     * opened. The toe0 netif is created after start (needs esp_netif). */
    ESP_ERROR_CHECK(wiznet_toe_init());
#endif

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *eth_netifs[eth_port_cnt];
    esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netifs[0] = esp_netif_new(&cfg);
        eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++) {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i*5;
            eth_netifs[i] = esp_netif_new(&cfg_spi);
            eth_netif_glues[i] = esp_eth_new_netif_glue(eth_handles[i]);
            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[i], eth_netif_glues[i]));
        }
    }

    // Register user defined event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

#if CONFIG_WIZNET_TOE_ECHO_DEMO
    // Intercept the MACRAW (socket-0) RX path to log each frame lwIP receives,
    // then forward it to the stack. Overrides the netif glue's input callback,
    // so re-forward via esp_netif_receive() ourselves (eth_handles[0]/eth_netifs[0]
    // is the single W5500 port). Then start the lwIP-side echo server.
    ESP_ERROR_CHECK(esp_eth_update_input_path(eth_handles[0], macraw_rx_input, eth_netifs[0]));
    xTaskCreate(lwip_echo_task, "lwip_echo", 4096, NULL, 5, NULL);
#endif

#if CONFIG_WIZNET_TOE_ENABLE
    // ioLibrary/TOE was initialized before esp_netif_init (see above); now that
    // esp_eth is started, expose the TOE path as the toe0 netif.
    uint8_t toe_mac[6] = {0};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handles[0], ETH_CMD_G_MAC_ADDR, toe_mac));
    toe_mac[5] ^= 0x01;

    s_toe_netif = wiznet_toe_netif_create(toe_mac);
    ESP_ERROR_CHECK(s_toe_netif != NULL ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "WIZnet ioLibrary + toe0 initialized (netif-level split baseline)");

    /* Report per-netif ifindex. (Do NOT esp_netif_action_start(toe0): toe0 is a
     * dummy/metadata netif with no real driver_handle, so esp_netif_start's
     * mandatory-config sanity check aborts -> boot loop. A valid TOE ifindex
     * will need toe0 to carry a minimal driver; handled when the BSD shim lands.) */
    int eth_ifindex = esp_netif_get_netif_impl_index(eth_netifs[0]);
    int toe_ifindex = esp_netif_get_netif_impl_index(s_toe_netif);
    ESP_LOGI(TAG, "netif ifindex -> eth0=%d, toe0=%d", eth_ifindex, toe_ifindex);

    /* BSD shim 라우팅 코어 sanity 체크: 정책상 TCP는 TOE로, UDP는 lwIP로 가야 함.
     * (네트워크 없이도 분기 결정이 맞는지 로그로 확인) */
    {
        int t = wiz_socket(AF_INET, SOCK_STREAM, 0);   /* 기대: TOE */
        int u = wiz_socket(AF_INET, SOCK_DGRAM, 0);    /* 기대: lwIP */
        ESP_LOGI(TAG, "wiz routing check -> TCP:%s(fd=%d)  UDP:%s(fd=%d)",
                 wiz_fd_is_toe(t) ? "TOE" : "lwIP", t,
                 wiz_fd_is_toe(u) ? "TOE" : "lwIP", u);
        wiz_close(t);
        wiz_close(u);
    }
#endif

#if CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S >= 0
    // For demonstration purposes, wait and then deinit Ethernet network
    vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S * 1000));
    ESP_LOGI(TAG, "stop and deinitialize Ethernet network...");
#if CONFIG_WIZNET_TOE_ENABLE
    ESP_ERROR_CHECK_WITHOUT_ABORT(wiznet_toe_dispatcher_stop());
#endif
    // Stop Ethernet driver state machine and destroy netif
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_stop(eth_handles[i]));
        ESP_ERROR_CHECK(esp_eth_del_netif_glue(eth_netif_glues[i]));
        esp_netif_destroy(eth_netifs[i]);
    }
    esp_netif_deinit();
    ESP_ERROR_CHECK(example_eth_deinit(eth_handles, eth_port_cnt));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler));
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
#endif // EXAMPLE_ETH_DEINIT_AFTER_S > 0
}
