#pragma once

#include <stddef.h>
#include "lwip/sockets.h"   /* struct sockaddr, socklen_t */

/*
 * BSD socket shim (라우팅 코어).
 *
 * 표준 BSD 시그니처와 동일한 함수를 제공하되, 내부에서 경로를 자동 분기한다:
 *   - 정책에 따라 TOE(ioLibrary, 소켓 1..7) 또는 lwIP(소켓 0/MACRAW)로 라우팅.
 *   - 반환되는 fd 는 "태그된" 정수: TOE fd 는 WIZ_TOE_FD_BASE 이상, lwIP fd 는 그 미만.
 *   - 이후 connect/send/recv/close 는 fd 태그를 보고 해당 경로로 디스패치.
 *
 * 최종 목표(Phase 4)는 이 함수들이 표준 socket()/send()/recv() 로 투명하게
 * 매핑되는 것이며, 현재 단계는 그 라우팅 코어다. (클라이언트 측 우선)
 */

#ifdef __cplusplus
extern "C" {
#endif

/* lwIP fd 와 겹치지 않는 TOE fd 시작값 (lwIP fd 는 보통 작은 정수). */
#define WIZ_TOE_FD_BASE 0x4000

int wiz_socket(int domain, int type, int protocol);
int wiz_connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
int wiz_send(int fd, const void *buf, size_t len, int flags);
int wiz_recv(int fd, void *buf, size_t len, int flags);
int wiz_close(int fd);

/* server-side */
int wiz_bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int wiz_listen(int fd, int backlog);
/* NOTE (W5500 TOE): the listening hardware socket itself becomes the
 * connection on accept, so wiz_accept() returns the SAME fd as `fd` (there is
 * no separate connection fd). After the connection closes, call wiz_listen()
 * again to re-arm. For the lwIP path it behaves like standard accept(). */
int wiz_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

/* fd 가 TOE 경로인지 (디버그/테스트용). */
int wiz_fd_is_toe(int fd);

#ifdef __cplusplus
}
#endif
