# Pre-lwIP-revive 백업 (Stage 1 시작 전 상태)

## 백업 시점
2026-05-21, **Stage 1: lwIP 살리기** 작업 직전.

## 무엇이 백업되어 있나
| 경로 | 원본 위치 |
|---|---|
| `managed_components/espressif__iperf/` | `iperf_TOE/managed_components/espressif__iperf/` |
| `managed_components/espressif__iperf-cmd/` | `iperf_TOE/managed_components/espressif__iperf-cmd/` |
| `main/ethernet_iperf_main.c` | `iperf_TOE/main/` |
| `main/CMakeLists.txt` | `iperf_TOE/main/` |
| `main/cmd_ethernet.[ch]` | `iperf_TOE/main/` |

## 백업한 이유
1. `managed_components/espressif__iperf/iperf.c`가 **표준 v0.1.3이 아니라 ioLibrary와 혼합된 hybrid 실험 코드**임. `idf.py reconfigure`로 갱신하면 날아감.
2. `main/ethernet_iperf_main.c`도 `esp_netif`/`esp_eth` 우회한 상태라 표준 코드와 다름.
3. Stage 1에서 표준 lwIP/esp_eth 흐름으로 되돌리면서 이전 시도의 흔적을 잃지 않기 위함.

## espressif__iperf/iperf.c 의 ioLibrary 흔적 (요약)

표준 v0.1.3 대비 다음 라인이 ioLibrary 함수로 교체/혼합되어 있음:

- **L21~24**: `#include "W5500/w5500.h"`, `"wizchip_conf.h"`, `"wizchip_spi.h"` 추가 (표준엔 없음)
- **L200~206 `recv_iperf()`** : `wiz_recv_data()` + `setSn_CR(Sn_CR_RECV)` 직접 호출
- **L226 `socket_recv()`** : `spi_device_acquire_bus(spi_dev, portMAX_DELAY)` SPI 락 직접 호출
- **L395 부근**: `getSIPR(ip)`로 W5500 IP 직접 조회 (`esp_netif_get_ip_info`가 아니라)
- **L400~411**: TCP server accept 루프가 `getSn_SR / disconnect / listen / socket(sn, Sn_MR_TCP, ..., 0x20)` 로 ioLibrary 소켓 상태 머신 사용 (BSD `accept()` 우회)

## 새 방향에서 이 코드의 위치

PPT 흐름과 사용자 확인에 따른 새 방향:
- 사용자 코드는 lwIP BSD socket을 entry point로 사용
- 내부에서 TOE/lwIP를 hybrid로 분기
- 즉 이 iperf.c의 hybrid 패턴은 **방향이 다름**. 표준 BSD socket 코드만 남기고 TOE 분기는 socket layer 아래에서 처리해야 함.

따라서 Stage 1에서 이 백업의 ioLibrary 흔적은 **재사용하지 않고**, 표준 espressif__iperf v0.1.3 으로 되돌릴 예정.
참고용으로만 보존.

## 복원 방법
```bash
# managed_components 복원
cp -r backup_pre_lwip_revive/managed_components/* managed_components/
# main 복원
cp backup_pre_lwip_revive/main/*.c backup_pre_lwip_revive/main/*.h main/
cp backup_pre_lwip_revive/main/CMakeLists.txt main/
```
