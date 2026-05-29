#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "wiznet_toe.h"

#if CONFIG_WIZNET_TOE_ECHO_DEMO

#include "socket.h"        /* SOCK_* status constants */
#include "wizchip_conf.h"
#include "toe_socket.h"

static const char *TAG = "toe_echo";
static bool s_started;
static uint16_t s_port;

#define TOE_ECHO_FIRST_SN  1
#define TOE_ECHO_LAST_SN   (_WIZCHIP_SOCK_NUM_ - 1)   /* 7 for W5500 */

static void toe_relisten(int sn)
{
    toe_sock_close(sn);
    if (toe_sock_open_n(sn, TOE_SOCK_TCP, s_port) >= 0) {
        toe_sock_listen(sn);
    }
}

/*
 * Echo pool: all TOE hardware sockets 1..7 LISTEN on the same port. The W5500
 * assigns each incoming connection to a free listening socket, so up to 7
 * simultaneous TOE clients are echoed. A single task polls every socket
 * (non-blocking) so one stack/buffer serves them all.
 */
static void toe_echo_task(void *arg)
{
    (void)arg;
    static uint8_t buf[1460];
    static uint8_t prev_sr[_WIZCHIP_SOCK_NUM_];

    ESP_LOGI(TAG, "TOE echo pool on port %u (sockets %d..%d)",
             s_port, TOE_ECHO_FIRST_SN, TOE_ECHO_LAST_SN);

    for (int sn = TOE_ECHO_FIRST_SN; sn <= TOE_ECHO_LAST_SN; sn++) {
        if (toe_sock_open_n(sn, TOE_SOCK_TCP, s_port) >= 0) {
            toe_sock_listen(sn);
        }
    }

    for (;;) {
        for (int sn = TOE_ECHO_FIRST_SN; sn <= TOE_ECHO_LAST_SN; sn++) {
            uint8_t sr = toe_sock_status(sn);

            if (sr == SOCK_ESTABLISHED && prev_sr[sn] != SOCK_ESTABLISHED) {
                ESP_LOGI(TAG, "socket %d connected", sn);
            }
            prev_sr[sn] = sr;

            if (sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT) {
                if (toe_sock_rx_available(sn) > 0) {
                    int n = toe_sock_recv(sn, buf, sizeof(buf));
                    if (n > 0) {
                        toe_sock_send(sn, buf, (size_t)n);
                    } else {
                        ESP_LOGI(TAG, "socket %d closed, re-listening", sn);
                        toe_relisten(sn);
                    }
                } else if (sr == SOCK_CLOSE_WAIT) {
                    ESP_LOGI(TAG, "socket %d closed, re-listening", sn);
                    toe_relisten(sn);
                }
            } else if (sr == SOCK_CLOSED) {
                toe_relisten(sn);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t wiznet_toe_start_echo_server(uint16_t port)
{
    if (s_started) {
        return ESP_OK;
    }
    s_port = (port != 0) ? port : (uint16_t)CONFIG_WIZNET_TOE_ECHO_PORT;
    BaseType_t rc = xTaskCreate(toe_echo_task, "toe_echo", 4096, NULL, 5, NULL);
    if (rc != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

#else /* !CONFIG_WIZNET_TOE_ECHO_DEMO */

esp_err_t wiznet_toe_start_echo_server(uint16_t port)
{
    (void)port;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
