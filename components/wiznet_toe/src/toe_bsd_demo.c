#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "wiznet_toe.h"

#if CONFIG_WIZNET_TOE_BSD_SERVER_DEMO

#include "lwip/sockets.h"
#include "toe_bsd.h"

static const char *TAG = "bsd_echo";
static bool s_started;
static uint16_t s_port;

/*
 * TCP echo server written with the BSD shim. This is the SAME shape as a
 * standard BSD server (socket/bind/listen/accept/recv/send/close); the shim
 * routes it to the TOE hardware path. Compare against the known-good direct
 * TOE echo server using the same client-side test (PC connects to the board).
 */
static void bsd_echo_task(void *arg)
{
    (void)arg;
    static char buf[1460];

    int lfd = wiz_socket(AF_INET, SOCK_STREAM, 0);          /* TCP -> TOE */
    struct sockaddr_in a = { .sin_family = AF_INET };
    a.sin_port = htons(s_port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    wiz_bind(lfd, (struct sockaddr *)&a, sizeof(a));

    ESP_LOGI(TAG, "BSD-shim echo server on :%u (fd=%d, path=%s)",
             s_port, lfd, wiz_fd_is_toe(lfd) ? "TOE" : "lwIP");

    for (;;) {
        if (wiz_listen(lfd, 1) != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int cfd = wiz_accept(lfd, NULL, NULL);   /* W5500 TOE: cfd == lfd */
        if (cfd < 0) {
            continue;
        }
        ESP_LOGI(TAG, "client connected (fd=%d)", cfd);
        for (;;) {
            int n = wiz_recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            wiz_send(cfd, buf, (size_t)n, 0);
        }
        ESP_LOGI(TAG, "client closed, re-listening");
        wiz_close(cfd);
    }
}

esp_err_t wiznet_toe_start_bsd_echo(uint16_t port)
{
    if (s_started) {
        return ESP_OK;
    }
    s_port = (port != 0) ? port : (uint16_t)CONFIG_WIZNET_TOE_BSD_SERVER_DEMO_PORT;
    if (xTaskCreate(bsd_echo_task, "bsd_echo", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

#else

esp_err_t wiznet_toe_start_bsd_echo(uint16_t port)
{
    (void)port;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
