#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "toe_socket.h"
#include "toe_bsd.h"

static const char *TAG = "wiz_bsd";

static inline bool fd_is_toe(int fd)
{
    return fd >= WIZ_TOE_FD_BASE;
}

static inline int fd_to_sn(int fd)
{
    return fd - WIZ_TOE_FD_BASE;
}

int wiz_fd_is_toe(int fd)
{
    return fd_is_toe(fd) ? 1 : 0;
}

/*
 * 경로 정책. 지금은 단순: AF_INET + TCP(SOCK_STREAM) -> TOE, 그 외 -> lwIP.
 * (UDP 유니캐스트->TOE / 멀티캐스트->lwIP 등은 dest 를 아는 시점이 필요하므로
 *  뒤 단계에서 connect/sendto 시점에 정교화한다.)
 */
static bool should_use_toe(int domain, int type, int protocol)
{
    (void)protocol;
    return (domain == AF_INET && type == SOCK_STREAM);
}

int wiz_socket(int domain, int type, int protocol)
{
    if (should_use_toe(domain, type, protocol)) {
        int sn = toe_sock_open(TOE_SOCK_TCP, 0);   /* ephemeral local port */
        if (sn >= 1) {
            ESP_LOGI(TAG, "socket() -> TOE (sn=%d, fd=%d)", sn, WIZ_TOE_FD_BASE + sn);
            return WIZ_TOE_FD_BASE + sn;
        }
        /* TOE 슬롯 부족 -> lwIP fallback */
        ESP_LOGW(TAG, "no free TOE socket, falling back to lwIP");
    }
    int fd = lwip_socket(domain, type, protocol);
    ESP_LOGI(TAG, "socket() -> lwIP (fd=%d)", fd);
    return fd;
}

int wiz_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (fd_is_toe(fd)) {
        if (addr == NULL || addr->sa_family != AF_INET) {
            return -1;
        }
        const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
        uint32_t h = lwip_ntohl(a->sin_addr.s_addr);
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                 (unsigned)((h >> 24) & 0xFF), (unsigned)((h >> 16) & 0xFF),
                 (unsigned)((h >> 8) & 0xFF), (unsigned)(h & 0xFF));
        uint16_t port = lwip_ntohs(a->sin_port);

        if (toe_sock_connect(fd_to_sn(fd), ip, port) != ESP_OK) {
            return -1;
        }
        return (toe_sock_wait_connected(fd_to_sn(fd), 5000) == ESP_OK) ? 0 : -1;
    }
    return lwip_connect(fd, addr, addrlen);
}

int wiz_send(int fd, const void *buf, size_t len, int flags)
{
    if (fd_is_toe(fd)) {
        return toe_sock_send(fd_to_sn(fd), buf, len);
    }
    return lwip_send(fd, buf, len, flags);
}

int wiz_recv(int fd, void *buf, size_t len, int flags)
{
    if (fd_is_toe(fd)) {
        int sn = fd_to_sn(fd);
        /* BSD blocking-recv semantics over the non-blocking TOE layer: wait
         * (yielding) until data arrives, then return it; return 0 only once the
         * peer has closed AND the RX buffer is drained. */
        for (;;) {
            if (toe_sock_rx_available(sn) > 0) {
                return toe_sock_recv(sn, buf, len);
            }
            if (!toe_sock_connected(sn)) {
                return 0;                     /* closed / CLOSE_WAIT, drained */
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return lwip_recv(fd, buf, len, flags);
}

int wiz_close(int fd)
{
    if (fd_is_toe(fd)) {
        toe_sock_close(fd_to_sn(fd));
        return 0;
    }
    return lwip_close(fd);
}

/* ---- server side ---------------------------------------------------------- */

/* Per-TOE-socket bound port (set by wiz_bind, applied at wiz_listen). */
static uint16_t s_bind_port[8];

int wiz_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (fd_is_toe(fd)) {
        if (addr != NULL && addr->sa_family == AF_INET) {
            s_bind_port[fd_to_sn(fd) & 0x7] =
                lwip_ntohs(((const struct sockaddr_in *)addr)->sin_port);
        }
        return 0;
    }
    return lwip_bind(fd, addr, addrlen);
}

int wiz_listen(int fd, int backlog)
{
    if (fd_is_toe(fd)) {
        int sn = fd_to_sn(fd);
        uint16_t port = s_bind_port[sn & 0x7];
        /* (Re)open the hardware socket on the bound port, then LISTEN. The
         * W5500 sets the local port at socket-open time, so we reopen here. */
        toe_sock_close(sn);
        if (toe_sock_open_n(sn, TOE_SOCK_TCP, port) < 0) {
            return -1;
        }
        return (toe_sock_listen(sn) == ESP_OK) ? 0 : -1;
    }
    return lwip_listen(fd, backlog);
}

int wiz_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (fd_is_toe(fd)) {
        /* Wait until a client connects (LISTEN -> ESTABLISHED). The listening
         * socket itself becomes the connection -> return the same fd. */
        if (toe_sock_wait_connected(fd_to_sn(fd), -1) != ESP_OK) {
            return -1;
        }
        (void)addr;
        (void)addrlen;
        return fd;
    }
    return lwip_accept(fd, addr, addrlen);
}
