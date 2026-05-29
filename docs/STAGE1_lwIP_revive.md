# Stage 1: lwIP 살리기 — 변경 내역 & 빌드 가이드

> 통합 마스터플랜의 1단계. 표준 ESP-IDF lwIP/esp_eth 흐름을 복구해서, BSD socket으로 ping/iperf가 동작하는 깨끗한 기준선을 만든다.
> 다음 단계: TOE hybrid shim 도입(Phase 4 of 마스터플랜).

---

## 1. 무엇이 바뀌었나

### 수정한 파일

| 파일 | 변경 요지 |
|---|---|
| `main/ethernet_iperf_main.c` | ioLibrary 직접 호출 제거. `ethernet_init_all()` + `esp_netif`/`esp_eth` 표준 흐름으로 재작성. ETH/IP 이벤트 핸들러 추가. |
| `main/cmd_ethernet.c` | ioLibrary 헤더/구조체 제거. `esp_netif_get_handle_from_ifkey("ethN")` + `esp_netif_get_ip_info()` 로 정보 출력. |
| `main/CMakeLists.txt` | `lib/ioLibrary_Driver/*`, `port/ioLibrary_Driver/*`, `lib/iperf/*` 소스 모두 빌드 제외. `PRIV_REQUIRES`에 `esp_eth`, `esp_event`, `console`, `vfs`, `ethernet_init` 명시. |

### 비활성화 (이름 변경)

| 경로 | 처리 |
|---|---|
| `managed_components/espressif__iperf/` | → `managed_components/espressif__iperf.disabled-by-stage1/` 로 rename. 이유: 이 폴더의 `iperf.c`가 표준 v0.1.3이 아닌 **ioLibrary 혼합 버전**이라 빌드를 깨뜨림. `idf.py reconfigure`로 자동 재다운로드 받음. |

### 백업

| 위치 | 내용 |
|---|---|
| `backup_pre_lwip_revive/managed_components/espressif__iperf/` | ioLibrary 혼합 버전 전체 |
| `backup_pre_lwip_revive/managed_components/espressif__iperf-cmd/` | (보존용, 미수정) |
| `backup_pre_lwip_revive/main/*.c, *.h, CMakeLists.txt` | 변경 전 main 코드 |
| `backup_pre_lwip_revive/README.md` | 백업 사유 + ioLibrary 흔적 라인 목록 |

### 손대지 않은 것 (의도적)

- `lib/ioLibrary_Driver/` — 폴더 그대로 둠. 빌드에서만 빠짐. 다음 단계에서 `components/wiznet_toe/` 로 옮길 예정.
- `port/ioLibrary_Driver/` — 동일.
- `components/esp_eth/` — IDF stock 구조 그대로. 패치 흔적이 있으면 다음 단계에서 git diff로 확인 필요. **개인적인 생각으로는** 이 폴더는 통째로 복사된 것일 뿐 실 패치는 없을 가능성 큼.
- `sdkconfig` — 그대로 사용. `CONFIG_EXAMPLE_USE_SPI_ETHERNET=y`, `CONFIG_ETH_SPI_ETHERNET_W5500=y`, SPI 핀/클럭(40MHz) 모두 이미 정상 값.
- `espressif/iperf-cmd` — 표준 그대로. 손댄 흔적 없음 확인.
- `dependencies.lock` — 그대로. `idf.py reconfigure` 시 ESP-IDF가 자동으로 재해석.

---

## 2. Stage 1 후 동작 흐름 (텍스트)

```
app_main()
    ├─ initialize_filesystem()                    [history.txt 저장용]
    ├─ esp_console_new_repl_uart()                [REPL 준비]
    └─ init_ethernet_and_netif()
            ├─ esp_netif_init()                   [lwIP 시동]
            ├─ esp_event_loop_create_default()
            ├─ ethernet_init_all(&handles, &cnt)  [sdkconfig 기반 W5500 SPI driver 설치]
            ├─ for each port:
            │     esp_netif_new(ESP_NETIF_DEFAULT_ETH 기반)
            │     esp_netif_attach(netif, esp_eth_new_netif_glue(handle))
            ├─ esp_event_handler_register(ETH_EVENT, ...)
            ├─ esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ...)
            └─ for each port: esp_eth_start(handle)   [DHCP 시도]
```

확실하게 말하면, 이 흐름은 ESP-IDF `examples/ethernet/basic/main` 과 동일한 표준 패턴이야.

---

## 3. 빌드 / 플래시 / 검증 절차

### 3-1. 빌드 (네가 PC에서)

```powershell
# 1) iperf_TOE 폴더로 이동
cd C:\Users\lihan\Desktop\클로드\iperf_TOE

# 2) 이전 빌드 정리 (managed_components 변경 반영 위해)
idf.py fullclean

# 3) ESP-IDF 가 표준 espressif/iperf v0.1.3 자동 재다운
idf.py reconfigure

# 4) 빌드
idf.py build
```

### 3-2. 플래시 & 모니터

```powershell
idf.py -p COMx flash monitor
```

### 3-3. 기대 로그 (성공 시)

```
I (xxx) eth_iperf: Ethernet devices found: 1
I (xxx) eth_iperf: Eth started
I (xxx) eth_iperf: Eth link UP  MAC xx:xx:xx:xx:xx:xx
I (xxx) esp_netif_handlers: eth0 ip: 192.168.x.x, mask: 255.255.255.0, gw: 192.168.x.1
I (xxx) eth_iperf: GOT IP   : 192.168.x.x
I (xxx) eth_iperf: Mask     : 255.255.255.0
I (xxx) eth_iperf: Gateway  : 192.168.x.1

iperf>
```

### 3-4. 검증 체크리스트

| # | 항목 | 확인 방법 | 합격 |
|---|---|---|---|
| 1 | 빌드 성공 | `idf.py build` 에러 없이 끝남 | ✅ |
| 2 | W5500 link up | 모니터에 `Eth link UP` | ✅ |
| 3 | DHCP IP 획득 | `GOT IP : ...` 로그 | ✅ |
| 4 | console 명령 동작 | `iperf>` 프롬프트에서 `ethernet info` → IP/MAC 출력 | ✅ |
| 5 | PC에서 ping 응답 | PC에서 `ping 192.168.x.x` → 응답 옴 | ✅ |
| 6 | 표준 iperf TCP 동작 | DUT: `iperf -s` / PC: `iperf -c <DUT_IP> -t 10` | ✅ |
| 7 | 표준 iperf UDP 동작 | DUT: `iperf -s -u` / PC: `iperf -c <DUT_IP> -u -t 10 -b 50M` | ✅ |

5~7 중 하나라도 실패하면 알려줘. 다음 단계 전에 짚고 가야 해.

---

## 4. 알려진 잠재 이슈

### 4-1. `components/esp_eth/` override 충돌 (中)

`components/esp_eth/`가 프로젝트 안에 있으면 ESP-IDF는 IDF stock 대신 이걸 우선 사용해. **개인적인 생각으로는** 이 폴더는 단순 복사본이라 동작에 차이 없을 가능성이 크지만, 만약 빌드 에러가 나거나 W5500이 안 잡히면 이 폴더부터 의심해.

이부분은 내 생각은 이러한데 너가 한번 더 확인을 해보는걸 추천할게: 빌드 중 `esp_eth` 관련 warning이나 `multiple definition` 에러 나면 알려줘. 그러면 IDF stock 으로 되돌리는 작업 추가.

### 4-2. dependencies.lock 재계산

`fullclean` + `reconfigure` 하면 lock 파일이 새로 만들어지면서 컴포넌트 버전이 살짝 바뀔 수 있어. `espressif/iperf` 가 0.1.3 → 0.1.4 같은 minor 업그레이드 정도라면 무해. major 바뀌면 알려줘.

### 4-3. SPI 핀 충돌

sdkconfig 에 정의된 핀(SCLK=21, MOSI=47, MISO=38, CS=18, INT=10, RST=17)이 실제 보드 배선과 맞는지 확인. `ethernet_init_all` 이 -1 리턴하면 SPI bus 또는 GPIO 문제.

### 4-4. 멀티 인터페이스 라우팅

`ethernet_init_all`이 1개 이상 port를 잡으면 (e.g. W5500 + W6300 동시) `route_prio`로 우선순위가 정해짐. 한 개만 쓸 거면 무시 가능.

---

## 5. 다음 단계 (Stage 2 예고)

Stage 1 검증이 끝나면 이어서:

- **Stage 2**: `lib/ioLibrary_Driver` 와 `port/ioLibrary_Driver` 를 `components/wiznet_toe/` 컴포넌트로 분리. 빌드만 되고 main은 호출 안 함.
- **Stage 3**: SPI 버스/INT 핀 공유 — esp_eth(MACRAW) 와 ioLibrary(TOE) 가 같은 W5500에 안전하게 공존하도록 mutex/dispatcher 정비.
- **Stage 4**: VFS shim 도입 — BSD `socket()` 호출 시 자동 분기 (TCP → TOE, UDP unicast → TOE, UDP multicast → lwIP).

마스터플랜: `C:\Users\lihan\Desktop\클로드\ESP_IDF_TOE_LWIP_통합_계획서.md` Phase 1~7.

---

## 6. 롤백 방법

만약 Stage 1이 잘못 되어 되돌리고 싶으면:

```powershell
# 1) main 복원
copy backup_pre_lwip_revive\main\*.c main\
copy backup_pre_lwip_revive\main\*.h main\
copy backup_pre_lwip_revive\main\CMakeLists.txt main\

# 2) managed_components 복원
ren managed_components\espressif__iperf.disabled-by-stage1 espressif__iperf
xcopy /E /Y backup_pre_lwip_revive\managed_components\espressif__iperf managed_components\espressif__iperf\

# 3) 재빌드
idf.py fullclean
idf.py build
```
