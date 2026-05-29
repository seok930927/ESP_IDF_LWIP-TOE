# ESP-IDF lwIP에 WIZnet ioLibrary(W5500 TOE) 통합 계획서

> **대상 프로젝트**: `iperf_TOE` (ESP-IDF + W5500 + ioLibrary_Driver)
> **통합 방식**: PPT Slide 4 — netif 레벨 TOE 등록 (Hybrid)
> **작성일**: 2026-05-21

---

## 1. 결정 사항 요약

| 항목 | 결정 |
|---|---|
| 통합 위치 | **netif 레벨 TOE 등록** (`TOE_eth (esp_netif_t)` 추가) |
| TCP | **무조건 TOE 경로** (W5500 hardware TCP/IP) |
| UDP unicast | **TOE 경로** (개인적인 의견) |
| UDP multicast / broadcast | **lwIP 경로 (MACRAW)** (개인적인 의견) |
| 기존 esp_eth / MACRAW | **유지** (Hybrid). 소켓 0 = MACRAW → lwIP, 소켓 1~7 = TOE |
| ARP / DHCP / ICMP | lwIP가 MACRAW를 통해 처리 |
| 사용자 API | BSD socket (`lwip/sockets.h`) 그대로 유지 |

---

## 2. 핵심 아키텍처

### 2.1 큰 그림

```
                     사용자 코드  (BSD socket API)
                            │
                  socket() / connect() / send() / recv()
                            │
                ┌───────────▼───────────┐
                │   lwip/sockets.c       │
                │   (★ TOE shim 삽입)    │
                └─┬───────────────────┬─┘
                  │                   │
        TOE netif 바인딩?         일반 fd
                  │                   │
                  ▼                   ▼
        ┌──────────────┐    ┌──────────────────┐
        │ TOE shim     │    │ lwIP TCP/IP      │
        │ (ioLibrary)  │    │ (tcpip_task)     │
        │ socket 1~7   │    │                  │
        └──────┬───────┘    └────────┬─────────┘
               │                     │
               │              esp_eth + netif glue
               │              (소켓 0 = MACRAW)
               │                     │
               └──────┬──────────────┘
                      ▼
                  W5500 chip
                  (단일 SPI 버스, mutex 공유)
```

### 2.2 두 개의 netif

소켓 0(MACRAW)을 통한 기존 `esp_eth` netif는 **그대로** 둔다. 여기에 추가로 ioLibrary 기반의 두 번째 netif (`TOE_eth`)를 등록한다.

- `eth0` (기존, MACRAW): lwIP가 L2~L4 전부 처리. ARP, DHCP, ICMP, mDNS 등.
- `toe0` (신규, TOE): L2~L4를 W5500이 처리. lwIP는 이 netif를 "투명한 통로"로만 봄.

확실하게 말하면 `toe0`는 **lwIP 관점에서는 일반 ethernet netif처럼 등록되지만, 패킷이 실제로 이 netif의 linkoutput()까지 내려오지는 않는다**. 우리가 `sockets.c` 상위에서 미리 분기하기 때문이다.

> **개인적인 생각으로는** netif 등록 자체는 "사용자/lwIP에게 TOE 경로의 IP를 노출하기 위한 메타 정보 컨테이너" 역할이 본질이야. ifindex, IP, MAC, 라우팅 정보를 lwIP가 인식하게 만들어주는 게 주 목적이고, 실제 패킷 처리는 shim이 가로채는 구조가 가장 자연스러워.

### 2.3 fd 분기 키 (가장 중요)

`sockets.c` 레벨에서 어떻게 "이 fd는 TOE야"를 알아낼지가 통합의 핵심.

**선택지 A** (권장): `socket()` 호출 시 분기 결정
- `socket(AF_INET, SOCK_STREAM, 0)` → 기본은 lwIP fd
- `socket(AF_INET, SOCK_STREAM, 0)` 후 `setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, "toe0")` → TOE fd로 전환
- 또는 `IP_BOUND_IF` ioctl로 netif index 지정

**선택지 B**: 별도 도메인 사용
- `socket(AF_TOE, SOCK_STREAM, 0)` 같은 커스텀 도메인 enum 도입
- 명시적이지만 BSD 호환성 깨짐

**선택지 C**: 정책 기반 자동 분기
- TCP면 무조건 TOE, UDP unicast면 TOE, UDP multicast면 lwIP — 자동 결정
- 사용자 코드는 손도 안 댐. 가장 깔끔하지만 디버깅/제어가 어려움

**개인적인 생각으로는** 선택지 A + C의 조합이 좋아. 기본 정책은 자동(C)으로 굴리고, `setsockopt`로 명시 오버라이드(A)를 허용하는 형태. iperf 같은 표준 코드도 무수정으로 동작하면서 필요할 때 수동 제어가 가능해.

---

## 3. UDP 처리 위치 — 추천안과 근거

확실하게 말하면 TCP는 TOE가 모든 면에서 유리해. UDP는 좀 더 갈래가 있어.

### 3.1 W5500 UDP 소켓 특성

- Hardware UDP 소켓: 페이로드까지 W5500 내부 버퍼(16KB)에 누적, `Sn_DIPR`/`Sn_DPORT`에 dest 적재 후 SEND
- IP/UDP checksum 하드웨어 처리
- 한 번의 SPI burst로 큰 페이로드 송수신 가능
- IGMPv1/v2 multicast 가입 가능하나 호환성·동시 그룹 수 제한 있음
- broadcast 가능

### 3.2 비교

| 항목 | TOE UDP | lwIP UDP (MACRAW) |
|---|---|---|
| 페이로드당 SPI 트랜잭션 | 1회 (큰 burst) | 매 프레임 (MTU 단위 분할 가능) |
| CPU 부하 | 낮음 | 높음 (lwIP UDP/IP 파싱) |
| RX 경로 길이 | ISR → user task | ISR → emac_task → tcpip_task → user task |
| Multicast | IGMPv1/v2 한정 | 완전 지원 |
| 동시 소켓 수 | 최대 7개 공유 | 사실상 제한 없음 |
| sendto/recvfrom 호환성 | 직접 매핑 가능 | 완전 호환 |
| iperf-UDP 성능 | 우위 (예상) | 한참 낮음 |

### 3.3 추천

확실하게 말하면 UDP unicast는 TOE로 보내는 게 throughput에서 명확한 이득이 있어. SPI 트랜잭션 횟수와 task 호핑 단계가 줄어드니까.

개인적인 생각으로는 multicast/broadcast UDP는 lwIP 경로에 남겨두는 게 좋아. mDNS, SSDP, NTP broadcast 등 표준 stack 호환성이 중요한 경우에 W5500의 IGMP 제약이 발목을 잡을 수 있어.

이부분은 내 생각은 이러한데 너가 한번 더 확인을 해보는걸 추천할게: 실제 사용처에서 multicast UDP를 쓸 일이 거의 없다면 단순화를 위해 **모든 UDP를 TOE로 일원화**해도 돼. 운용 시나리오 한 번 확인해줘.

---

## 4. 단계별 로드맵 (Phase 0 ~ Phase 7)

### Phase 0 — 베이스라인 측정 및 정리 (1~2일)

**목표**: 손대기 전 현재 상태를 정량/정성적으로 고정.

작업:
- 현재 `ethernet_iperf_main.c`는 esp_netif 거의 안 쓰고 ioLibrary 직접 호출 → lwIP 경로 자체가 사용 안 됨을 문서로 확정.
- iperf TCP/UDP 양방향 throughput 베이스라인 측정 (현재 ioLibrary 직접 호출 상태).
- esp_eth + lwIP 경로만 사용한 비교용 베이스라인도 측정 (`sdkconfig.ci.default_w5500` 기반).
- 두 값의 차이를 표로 정리.

산출물:
- `docs/baseline_perf.md` (TCP TX/RX, UDP TX/RX, 4가지 throughput)

검증:
- iperf 측정값의 분산 < 5%, 3회 평균.

### Phase 1 — 디렉토리/빌드 구조 재편 (1일)

**목표**: ioLibrary와 lwIP shim 코드가 같이 살 수 있는 컴포넌트 구조 마련.

작업:
- `components/wiznet_toe/` 신규 컴포넌트 생성
  - `lib/ioLibrary_Driver/`를 이쪽으로 이동(또는 git subtree).
  - `port/ioLibrary_Driver/`도 같이 이동.
  - `idf_component.yml`에서 `wiznet_toe` 의존성 추가.
- `components/esp_eth/`는 stock으로 되돌리고 (현재 복사된 이유 파악 필요), TOE shim 코드를 esp_eth에 섞지 않음 → 향후 IDF 업그레이드 시 분리 유지.

산출물:
- 새 컴포넌트 트리, CMakeLists.txt, Kconfig

검증:
- 빌드 통과, 기존 iperf 명령 정상 동작 (`pytest_eth_iperf.py` 일부 케이스 PASS).

> **이부분은 내 생각은 이러한데 너가 한번 더 확인을 해보는걸 추천할게**: `components/esp_eth/`를 통째로 복사한 이유가 있을 거야. W5500 SPI driver 부분을 손댔는지 git diff로 확인해줘. 손댔다면 패치만 분리해서 stock esp_eth에 적용하는 식으로 가야 깔끔해.

### Phase 2 — SPI 버스 공유 / mutex 통일 (2~3일)

**목표**: ioLibrary와 esp_eth(MACRAW)가 같은 SPI 버스를 충돌 없이 공유.

이게 Hybrid의 가장 큰 위험 지점이야. 두 드라이버가 같은 W5500 칩의 SPI를 동시에 두드리면 레지스터가 깨져.

작업:
- 현재 `wizchip_spi.c`의 `spi_mutex` 사용 패턴을 esp_eth W5500 SPI 드라이버(`esp_eth_mac_w5500.c`)와 통일.
- 단일 글로벌 SPI mutex로 통합 (esp_eth 쪽 SPI device handle을 wiznet_toe 컴포넌트도 공유).
- 또는 `spi_device_acquire_bus()` / `spi_device_release_bus()`로 ESP-IDF 표준 락 사용.
- W5500의 BSB(Block Select Bits) 충돌 검토: MACRAW가 소켓 0의 TX/RX FIFO를 쓰는 동안 TOE가 소켓 1~7 레지스터에 접근하는 건 OK (다른 BSB). 공통 레지스터(MR, IR, IMR 등) 접근 시는 락 필수.

산출물:
- `components/wiznet_toe/port/spi_lock.c` (mutex/락 유틸 통일)
- 수정: `port/ioLibrary_Driver/src/wizchip_spi.c`, `components/esp_eth/src/spi/esp_eth_mac_w5500.c`

검증:
- SPI 동시 접근 부하 테스트: MACRAW로 ARP/ICMP 트래픽 흘리면서 TOE 소켓 7개 동시 TCP echo 동작 → 24시간 무중단.
- W5500 레지스터 dump를 주기적으로 떠서 corruption 없는지 확인.

리스크:
- 소켓별 인터럽트 비트(Sn_IR)와 공통 IR 사이의 race. 양쪽 드라이버 모두 IR 읽기/쓰기 시점 통제 필요.

### Phase 3 — TOE netif 등록 (`toe0`) (2일)

**목표**: lwIP/esp_netif에 ioLibrary 기반 TOE netif를 두 번째 인터페이스로 등록.

작업:
- `esp_netif_new()`로 `toe0` 생성.
- Custom `esp_netif_netstack_config_t` 작성: linkoutput은 더미(no-op 또는 에러 리턴)로 둠 — 어차피 shim이 위에서 가로채니까 lwIP가 여기까지 패킷을 내려보내면 안 됨.
- MAC 주소: W5500은 MACRAW와 TOE가 같은 칩 MAC을 공유 → `eth0`와 동일 MAC을 `toe0`에도 설정. (또는 별도 가상 MAC 사용 — 후술)
- IP는 정적 할당 또는 DHCP. DHCP는 `eth0` 쪽에서 받은 IP를 `toe0`에도 복제하는 헬퍼 작성.
- ifindex 확보 → 사용자 코드에서 `IP_BOUND_IF` 사용 가능.

산출물:
- `components/wiznet_toe/src/toe_netif.c`
- API: `esp_netif_t *wiznet_toe_netif_create(const wiznet_toe_config_t *cfg);`

검증:
- `esp_netif_get_ifindex(toe0)`, `esp_netif_get_ip_info(toe0)`가 정상 값 반환.
- `ifconfig` 류 명령으로 두 netif 확인 가능.

> **개인적인 생각으로는** MAC을 어떻게 처리할지가 미묘해. 같은 칩이라 물리적으로는 MAC이 하나뿐인데, lwIP에 같은 MAC을 가진 netif 두 개가 있으면 ARP 응답 시 어느 netif로 갈지 헷갈릴 수 있어. 다행히 우리는 TOE 경로가 ARP를 직접 처리(W5500 hardware ARP)하니까 lwIP의 ARP 테이블과 분리되어 있어서 큰 문제는 없을 거야. 그래도 한 번 검증 필요.

### Phase 4 — `sockets.c` shim 삽입 (3~5일, 핵심 작업)

**목표**: BSD socket API 호출을 fd별로 TOE/lwIP로 분기.

이게 사용자가 말한 "LWIP 쪽에 IO라이브러리를 우겨 넣는다"의 실체. lwIP 컴포넌트는 ESP-IDF 관리 컴포넌트이므로 직접 수정보다는 **shim/override** 방식을 권장.

#### 4.1 후킹 방식 결정 (개인적인 생각으로는 (b) 추천)

**(a) ESP-IDF lwIP 컴포넌트 patch**
- `managed_components/espressif__lwip` 안의 `sockets.c`를 직접 수정.
- 가장 직접적이지만 IDF 업데이트 시마다 재패치 필요.

**(b) VFS 레이어 활용** ⭐ 권장
- ESP-IDF의 VFS(가상 파일시스템)는 fd 별로 driver를 갖는 구조.
- lwIP socket fd 범위와 분리된 별도 fd 범위를 등록 → `esp_vfs_register_with_id()`.
- 사용자 코드 입장에선 같은 `socket()`/`read()`/`write()`/`close()` 사용. VFS가 자동 분기.
- IDF 업데이트에 거의 영향 없음.

**(c) `LWIP_HOOK_*` 매크로 활용**
- lwIP가 제공하는 hook (`LWIP_HOOK_TCP_INPACKET_PCB` 등)을 sdkconfig에서 활성화.
- L4 단에서 분기 가능하지만 BSD socket layer에는 직접 영향 없음.

#### 4.2 VFS 기반 구현 (권장안 상세)

```c
// components/wiznet_toe/src/toe_vfs.c
static const esp_vfs_t toe_vfs = {
    .flags     = ESP_VFS_FLAG_DEFAULT,
    .open      = toe_open,      // 사용 안 함 (socket이 entry point)
    .read      = toe_read,
    .write     = toe_write,
    .close     = toe_close,
    .ioctl     = toe_ioctl,
    .fcntl     = toe_fcntl,
    // BSD socket용 확장
};

int toe_socket(int domain, int type, int proto) {
    // ioLibrary socket() 호출, sn 할당
    uint8_t sn = alloc_toe_socket_slot();  // 1~7 중 빈 곳
    socket(sn, (type == SOCK_STREAM ? Sn_MR_TCP : Sn_MR_UDP), 0, 0);
    int fd = esp_vfs_register_fd(toe_vfs_id);
    map_fd_to_sn(fd, sn);
    return fd;
}
```

`socket()` 진입점 분기는 사용자 코드 수정 없이 하기 위해 (1) `socket()`을 weak symbol로 override하거나 (2) 매크로로 가로채는 wrapper 함수 사용:

```c
// 공개 헤더
#define socket(d,t,p)   toe_socket_wrapper((d),(t),(p))

int toe_socket_wrapper(int d, int t, int p) {
    if (should_use_toe(d, t, p)) return toe_socket(d, t, p);
    else return lwip_socket(d, t, p);
}
```

**`should_use_toe()` 정책 (자동 분기)**:
- TCP (`SOCK_STREAM`) → TOE
- UDP unicast (`SOCK_DGRAM` + 일반 IP) → TOE
- UDP multicast/broadcast → lwIP
- raw / packet socket → lwIP
- TOE 슬롯 부족(7개 full) → lwIP fallback

#### 4.3 매핑 테이블

```c
typedef struct {
    int       lwip_fd;       // ESP-IDF VFS가 발급한 fd
    uint8_t   wiz_sn;        // W5500 socket number (1~7)
    uint8_t   proto;          // TCP/UDP
    bool      nonblocking;
    SemaphoreHandle_t rx_sem; // RX event용
    // socket option mirroring
} toe_socket_entry_t;

static toe_socket_entry_t g_toe_table[CONFIG_WIZNET_TOE_MAX_SOCKETS]; // 보통 7
```

산출물:
- `components/wiznet_toe/src/toe_vfs.c`
- `components/wiznet_toe/src/toe_socket.c` (BSD↔ioLibrary 매핑)
- `components/wiznet_toe/include/wiznet_toe.h`

검증:
- 단위 테스트: TOE fd로 TCP echo (echo server / client), UDP echo.
- `should_use_toe` 정책 매트릭스 테스트 (TCP/UDP/multicast/broadcast 각각).

### Phase 5 — 이벤트/RX 인터럽트 통합 (2~3일)

**목표**: W5500 INT 핀 하나로 들어오는 인터럽트를 MACRAW용과 TOE용으로 분배.

작업:
- 기존 `emac_w5500_task`는 INT 발생 시 IR 레지스터 전체를 읽음 → 소켓 0 비트면 MACRAW, 1~7 비트면 TOE 이벤트.
- TOE 이벤트는 `g_toe_table[sn].rx_sem`에 sem_signal → `recv()`에서 `xSemaphoreTake` 하던 user task 깨움.
- 또는 별도 `toe_isr_task`를 만들고 emac_task와 분기하는 디스패처 도입.

산출물:
- `components/wiznet_toe/src/toe_isr.c`

검증:
- ESP-IDF logic analyzer 또는 GPIO toggle로 INT latency 측정.
- 동시 다중 TCP 연결 시 RX 누락 없음.

리스크:
- INT 핀이 level-triggered라 모든 IR 비트를 클리어해야 다음 INT가 들어옴. 한 쪽 드라이버가 자기 비트만 클리어하면 INT가 영원히 안 들어옴. → IR 클리어 책임을 공통 dispatcher에 집중.

### Phase 6 — DHCP / ARP / 라우팅 검증 (1~2일)

**목표**: Hybrid 환경에서 IP 획득과 트래픽 라우팅이 정상 동작.

작업:
- `eth0` (MACRAW)가 DHCP로 IP 획득.
- 획득한 IP/GW/Mask를 `toe0`에도 복사 (`esp_netif_set_ip_info`).
- W5500의 `wiz_NetInfo`에도 동일 값 적용 (TOE가 ARP를 직접 처리하니 GW 정보 필요).
- 라우팅 테이블: 같은 서브넷의 트래픽은 어느 netif로 갈지? → 사용자가 `IP_BOUND_IF`로 명시 안 하면 자동 분기 정책에 의해 결정. 기본 라우팅은 `eth0`을 default로.

산출물:
- `components/wiznet_toe/src/toe_ip_sync.c`

검증:
- DHCP로 받은 IP가 두 netif에 동기화.
- ping (lwIP ICMP로 응답) 정상.
- TOE TCP connect가 GW 통한 외부 서버까지 도달.

### Phase 7 — iperf 통합 및 성능 검증 (2~3일)

**목표**: 표준 iperf 코드(BSD socket 사용)가 무수정으로 TOE 경로를 타고, 성능 측정.

작업:
- `lib/iperf/iperf.c`가 ioLibrary socket을 직접 호출하던 부분을 BSD socket으로 교체 (또는 ESP-IDF 표준 iperf 컴포넌트로 대체).
- shim이 자동으로 TOE로 분기되어 성능이 나오는지 확인.
- 측정: TCP TX/RX, UDP TX/RX, 각 방향 throughput + CPU load.

산출물:
- `docs/perf_report.md` (베이스라인 vs TOE 통합 후 비교)
- iperf 자동화: `pytest_eth_iperf.py` 확장

검증 시나리오:
1. TCP throughput: iperf3 server (PC) ↔ ESP32 (client), 10초 × 5회, 평균
2. TCP throughput 역방향: ESP32 server ↔ PC client
3. UDP throughput: PPS / Mbps, packet loss %, jitter
4. UDP multicast: 정상 동작 여부 (lwIP 경로 검증)
5. CPU load: `vTaskGetRunTimeStats` 로 tcpip_task / emac_task / toe_isr_task 점유율
6. 메모리: heap 사용량 (lwIP pbuf 할당이 줄어드는지)

**목표 수치 (개인적인 생각)**:
- TCP throughput: 베이스라인 lwIP 경로 대비 **1.5~2배 이상** 향상 기대 (SPI burst 효율)
- TCP CPU load: tcpip_task 사용률 **절반 이하**
- UDP throughput: 베이스라인 lwIP 경로 대비 **2배 이상** 기대
- 메모리: lwIP TCP PCB / pbuf pool 사용량 감소

이부분은 내 생각은 이러한데 너가 한번 더 확인을 해보는걸 추천할게: SPI 클럭이 현재 어떻게 설정되어 있는지(`CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ`), 그게 throughput 상한이 될 가능성이 커. 40MHz 기준 W5500 hardware 한계가 약 25Mbps라서 그 이상은 안 나옴. 측정 전에 SPI 클럭 최대치부터 확정해줘.

---

## 5. 수정/추가할 파일 목록

### 신규 추가

| 경로 | 역할 |
|---|---|
| `components/wiznet_toe/CMakeLists.txt` | 컴포넌트 정의 |
| `components/wiznet_toe/Kconfig` | 옵션 (TOE 최대 소켓, 자동 분기 정책 등) |
| `components/wiznet_toe/include/wiznet_toe.h` | 공개 API |
| `components/wiznet_toe/src/toe_netif.c` | esp_netif 등록 |
| `components/wiznet_toe/src/toe_vfs.c` | VFS driver |
| `components/wiznet_toe/src/toe_socket.c` | BSD↔ioLibrary 매핑 |
| `components/wiznet_toe/src/toe_isr.c` | INT 디스패처 |
| `components/wiznet_toe/src/toe_ip_sync.c` | IP 동기화 |
| `components/wiznet_toe/port/spi_lock.c` | SPI mutex 통일 |
| `components/wiznet_toe/lib/ioLibrary_Driver/` | 기존 `lib/ioLibrary_Driver` 이동 |
| `docs/baseline_perf.md` | Phase 0 결과 |
| `docs/perf_report.md` | Phase 7 결과 |

### 수정

| 경로 | 수정 내용 |
|---|---|
| `port/ioLibrary_Driver/src/wizchip_spi.c` | mutex를 공통 SPI lock으로 교체, 미사용 함수 정리 |
| `components/esp_eth/src/spi/esp_eth_mac_w5500.c` | SPI lock 공유, IR 클리어 책임 분리 (필요 시) |
| `main/ethernet_iperf_main.c` | esp_netif 초기화 추가, `toe0` 등록 호출, ioLibrary 직접 호출 제거 |
| `main/CMakeLists.txt` | `wiznet_toe` 의존성 추가 |
| `lib/iperf/iperf.c` | ioLibrary socket → BSD socket 전환 (또는 표준 iperf로 교체) |
| `sdkconfig.defaults` | `LWIP_SO_BINDTODEVICE=y`, `LWIP_IPV4=y`, TOE 옵션 활성화 |

### 삭제 후보 (정리)

- `port/ioLibrary_Driver/src/wizchip_spi.c` 안의 QSPI/legacy 미사용 함수
- `components/esp_eth/` (stock으로 되돌릴 경우 — 단, 변경 사항 확인 후)

---

## 6. 리스크 / 이슈 분석

### 6.1 동시성 — SPI 버스 경합 (高)

두 드라이버가 같은 SPI 디바이스를 공유. mutex로 보호하지만:
- mutex 대기 동안 W5500 RX FIFO가 overflow 가능
- W5500 RX 버퍼는 소켓별로 분리되어 있어서 다른 소켓 영향 적지만, MACRAW 소켓 0의 16KB 버퍼는 빠르게 찰 수 있음

대응: SPI 클럭 최대화 + mutex hold time 최소화 + RX FIFO watermark 모니터링.

### 6.2 INT 핀 공유 (高)

W5500 INT 핀은 모든 소켓 이벤트가 OR된 단일 신호. 한 쪽이 IR 처리를 누락하면 INT가 영원히 stuck.

대응: 통합 INT 디스패처가 IR 전체를 읽고 → 비트별로 라우팅 → 모든 비트 클리어. 양쪽 드라이버는 이 디스패처에서 받은 이벤트만 처리.

### 6.3 TOE 소켓 수 제한 (中)

W5500은 소켓 8개 (TOE 7개 + MACRAW 1개). 동시 TCP 연결이 많은 서버형 워크로드면 부족.

대응: 자동 fallback (TOE 슬롯 full이면 lwIP로). 또는 W6300 등 소켓 수 더 많은 칩으로 마이그레이션.

### 6.4 ARP 일관성 (中)

TOE는 W5500 hardware ARP, MACRAW는 lwIP ARP. 두 ARP 캐시가 분리됨. 일시적으로 같은 dest에 대해 다른 MAC을 갖고 있을 수 있음.

대응: `eth0`에서 DHCP/ARP 정상화 후 일정 시간 뒤 `toe0` 활성화. 또는 W5500 hardware ARP를 강제로 invalidate하는 ioctl 추가.

### 6.5 BSD socket option 호환성 (中)

ioLibrary는 BSD socket의 일부 옵션을 지원 안 함:
- `MSG_PEEK`: 미지원 → shim에서 emulate (buffer 복사 후 안 빼는 식으로) 필요
- `SO_LINGER`: ioLibrary는 close 시 자동 처리 → 정책 매핑 필요
- `TCP_NODELAY`: Nagle 알고리즘 — W5500 hardware에 옵션 있음 (확인 필요)
- `SO_RCVTIMEO`/`SO_SNDTIMEO`: shim에서 timer 추가 구현

대응: 옵션 매핑 표 작성 후 미지원 옵션은 명시적으로 `errno=ENOPROTOOPT` 리턴.

### 6.6 디버깅 난이도 (中)

기존: `lwip` 로그 + `tcpdump` (linux측)으로 디버깅 가능. 새 구조: TOE 경로는 lwIP 로그에 안 잡힘.

대응: `wiznet_toe` 컴포넌트 자체 로그 채널 + W5500 SPI 트랜잭션 trace 옵션 + SPI 트래픽 캡처용 디버그 빌드 옵션.

### 6.7 lwIP 업데이트 충돌 (低)

managed_components의 lwIP가 업데이트되면 VFS 등록 방식이 깨질 가능성. (낮은 확률)

대응: VFS API는 ESP-IDF 공식 API라 안정적. lwIP 자체 수정 없이 VFS만 쓰면 영향 적음.

---

## 7. 성능 검증 계획 (요약)

### 7.1 측정 지표

- **Throughput**: Mbps (TCP/UDP × TX/RX × 4가지)
- **CPU Load**: task별 점유율 (FreeRTOS run-time stats)
- **Latency**: ping RTT (us), TCP connect time (ms)
- **Memory**: heap free, lwIP pool 사용량
- **Reliability**: 24시간 무중단, packet loss %

### 7.2 환경

- DUT: ESP32 + W5500 SPI 모듈, SPI 클럭 최대 (40MHz)
- 상대: PC (Linux, iperf3)
- 케이블: Cat5e 이상, 1m
- 같은 스위치, 100Mbps full duplex

### 7.3 시나리오 매트릭스

| # | 시나리오 | Phase 0 (베이스) | Phase 7 (TOE 통합) |
|---|---|---|---|
| 1 | iperf TCP TX | ☐ | ☐ |
| 2 | iperf TCP RX | ☐ | ☐ |
| 3 | iperf UDP TX | ☐ | ☐ |
| 4 | iperf UDP RX | ☐ | ☐ |
| 5 | TOE + MACRAW 동시 (UDP multicast on lwIP, TCP iperf on TOE) | — | ☐ |
| 6 | 7-socket 동시 TCP echo | — | ☐ |
| 7 | 24h 장기 안정성 | ☐ | ☐ |

### 7.4 합격 기준

- TCP throughput: 베이스라인 대비 +50% 이상
- 24h 무중단, 메모리 leak 없음
- 표준 iperf 코드 무수정 동작

---

## 8. 일정 (개인적인 예상)

| Phase | 작업 | 기간 |
|---|---|---|
| 0 | 베이스라인 측정 | 1~2일 |
| 1 | 컴포넌트 구조 | 1일 |
| 2 | SPI 공유 | 2~3일 |
| 3 | TOE netif 등록 | 2일 |
| 4 | sockets.c shim (★) | 3~5일 |
| 5 | INT 디스패처 | 2~3일 |
| 6 | DHCP/ARP/라우팅 | 1~2일 |
| 7 | iperf 통합 검증 | 2~3일 |
| **합계** | | **14~21일** |

Phase 4가 가장 무게가 크고 위험도도 높아. 여기에 버퍼 일정 더 잡는 게 좋아.

---

## 9. 확인 필요 사항 (네가 한 번 더 봐줘)

다음 항목들은 내 추론이 들어간 부분이라 네가 한 번 더 확인해줬으면 좋겠어:

1. **`components/esp_eth/`를 통째로 복사한 이유** — stock IDF 대비 어떤 패치가 들어가 있는지 git diff 확인 필요.
2. **현재 main의 ioLibrary 직접 호출 — 이게 final form인지, 아니면 임시 테스트 코드인지**. 만약 final이면 lwIP/esp_netif는 사실상 무용지물 상태고, 통합 후엔 BSD socket으로 다 갈아엎어야 함.
3. **UDP multicast 사용 시나리오** — 실제 운용에서 mDNS/SSDP 같은 멀티캐스트를 쓰는지. 안 쓴다면 UDP도 전부 TOE로 일원화하는 게 단순해.
4. **SPI 클럭 현재 설정** — `CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ`가 몇 MHz인지. W5500 hardware 한계(~25Mbps)에 가까운지.
5. **동시 TCP 연결 수의 최대치** — 7개로 충분한지. 더 필요하면 W6300 마이그레이션 검토.
6. **iperf 라이브러리의 출처** — `lib/iperf/iperf.c`가 ESP-IDF 표준 iperf인지, ioLibrary용으로 포팅된 별도 버전인지. 표준이면 통합 후 무수정으로 동작해야 함.

---

## 10. 다음 액션 제안

다음 중 어떤 걸 먼저 하면 좋을지 알려줘:

- (a) Phase 0 베이스라인 측정 스크립트부터 작성 — 측정 자동화 (`pytest_eth_iperf.py` 확장)
- (b) Phase 1 컴포넌트 구조 재편 — 디렉토리 이동과 CMakeLists 작성
- (c) Phase 4 shim 프로토타입부터 — 가장 위험한 부분 PoC로 검증
- (d) `components/esp_eth/` 패치 확인 — 본격 작업 전 현 상태 점검
