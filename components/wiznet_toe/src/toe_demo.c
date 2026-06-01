#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "wiznet_toe.h"

#if CONFIG_WIZNET_TOE_ECHO_DEMO

#include "esp_timer.h"
#include "socket.h"        /* SOCK_* status constants */
#include "wizchip_conf.h"
#include "toe_socket.h"

static const char *TAG = "toe_echo";
static bool s_started;
static uint16_t s_port;

#define TOE_ECHO_FIRST_SN  1
#define TOE_ECHO_LAST_SN   (_WIZCHIP_SOCK_NUM_ - 1)   /* 7 for W5500 */

/* Each pool socket listens on its own port (base, base+1, ...). Listening
 * multiple W5500 sockets on the SAME port causes state churn + blocking
 * re-listen that stalls the single poll loop. Distinct ports avoid that. */
static uint16_t sock_port(int sn)
{
    return (uint16_t)(s_port + (sn - TOE_ECHO_FIRST_SN));
}

static void toe_relisten(int sn)
{
    toe_sock_close(sn);
    if (toe_sock_open_n(sn, TOE_SOCK_TCP, sock_port(sn)) >= 0) {
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

    ESP_LOGI(TAG, "TOE echo pool: sockets %d..%d on ports %u..%u",
             TOE_ECHO_FIRST_SN, TOE_ECHO_LAST_SN,
             (unsigned)sock_port(TOE_ECHO_FIRST_SN), (unsigned)sock_port(TOE_ECHO_LAST_SN));

    for (int sn = TOE_ECHO_FIRST_SN; sn <= TOE_ECHO_LAST_SN; sn++) {
        if (toe_sock_open_n(sn, TOE_SOCK_TCP, sock_port(sn)) >= 0) {
            toe_sock_listen(sn);
        }
    }

    int64_t last_hb = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (now - last_hb >= 1000000) {   /* 1s heartbeat: are we looping? socket states? */
            last_hb = now;
            ESP_LOGI(TAG, "hb SR 1..7 = %02x %02x %02x %02x %02x %02x %02x",
                     toe_sock_status(1), toe_sock_status(2), toe_sock_status(3),
                     toe_sock_status(4), toe_sock_status(5), toe_sock_status(6),
                     toe_sock_status(7));
        }
        for (int sn = TOE_ECHO_FIRST_SN; sn <= TOE_ECHO_LAST_SN; sn++) {
            uint8_t sr = toe_sock_status(sn);

            if (sr == SOCK_ESTABLISHED && prev_sr[sn] != SOCK_ESTABLISHED) {
                ESP_LOGI(TAG, "socket %d connected", sn);
            }
            prev_sr[sn] = sr;

            if (sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT) {
                int avail = toe_sock_rx_available(sn);
                if (avail > 0) {
                    ESP_LOGI(TAG, "sock %d: avail=%d -> recv...", sn, avail);
                    int n = toe_sock_recv(sn, buf, sizeof(buf));
                    ESP_LOGI(TAG, "sock %d: recv=%d -> send...", sn, n);
                    int st = (n > 0) ? toe_sock_send(sn, buf, (size_t)n) : 0;
                    ESP_LOGI(TAG, "sock %d: send=%d DONE", sn, st);
                    if (n <= 0) {
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
