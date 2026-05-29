# CLAUDE Handoff (2026-05-29)

## 1) Objective and Architecture
- Target: ESP-IDF + single W5500 chip (shared SPI lines)
- Intended hybrid model:
  - MACRAW path: socket 0 (existing esp_eth/lwIP path)
  - TOE path: socket 1..7 (ioLibrary path)
  - `toe0` netif metadata path added for TOE-side exposure and IP sync

## 2) What is already implemented

### A. WIZnet component scaffold
- Added `components/wiznet_toe/` as standalone component.
- Vendored ioLibrary source at `components/wiznet_toe/lib/ioLibrary_Driver`.

### B. Wiring/config applied (sdkconfig)
- `CONFIG_EXAMPLE_USE_W5500=y`
- `CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO=8`
- `CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO=7`
- `CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO=6`
- `CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO=44`
- `CONFIG_EXAMPLE_ETH_SPI_INT0_GPIO=9`
- `CONFIG_EXAMPLE_ETH_SPI_PHY_RST0_GPIO=43`
- `CONFIG_WIZNET_TOE_ENABLE=y`
- `CONFIG_WIZNET_TOE_SPI_ACQUIRE_BUS=y`
- `CONFIG_WIZNET_TOE_DISPATCHER_ENABLE=y`
- `CONFIG_WIZNET_TOE_DISPATCHER_PERIOD_MS=2`

### C. toe0 metadata path + IP sync
- `toe0` netif create API implemented.
- ETH DHCP event sync implemented:
  - `eth0` IP -> `toe0` IP
  - `eth0` IP/GW/MASK -> W5500 netinfo

### D. SPI coexistence hardening
- Added SPI bus acquire/release around CS-asserted transaction window in wiznet SPI port.

### E. TOE dispatcher (phase-in)
- Added polling dispatcher for TOE sockets only (1..7):
  - reads `SIR`
  - checks socket bits 1..7 only
  - reads and clears `Sn_IR` for those sockets
- Socket 0 (MACRAW) intentionally untouched.
- Public APIs added:
  - `wiznet_toe_dispatcher_start()`
  - `wiznet_toe_dispatcher_stop()`
  - `wiznet_toe_register_socket_event_cb()`

### F. Build status
- Build passes.
- `basic.bin` generated.
- Latest observed app size around `0x60140` (about 62% free on smallest app partition).

## 3) Key files changed
- `main/CMakeLists.txt`
- `main/ethernet_example_main.c`
- `components/wiznet_toe/CMakeLists.txt`
- `components/wiznet_toe/Kconfig`
- `components/wiznet_toe/include/wiznet_toe.h`
- `components/wiznet_toe/src/wiznet_toe.c`
- `components/wiznet_toe/src/wiznet_toe_stub.c`
- `components/wiznet_toe/src/wiznet_spi_port.c`
- `components/wiznet_toe/src/toe_netif.c`
- `components/wiznet_toe/src/toe_dispatcher.c`
- `sdkconfig`
- `docs/WIZNET_TOE_CHANGELOG.md`
- `docs/WIZNET_TOE_HANDOFF.md`

## 4) Known caveats
- ioLibrary third-party source produces warnings in this toolchain; currently allowed to avoid blocking integration.
- Macro name conflict warning (`MR`) exists between W5500 headers and Xtensa register headers; currently warning-only.
- Dispatcher is polling-based and not yet unified with esp_eth interrupt ownership.

## 5) Immediate next tasks
1. Integrate shared SPI/INT ownership policy with esp_eth path:
   - define authoritative IR clear order and ownership for socket 0 vs sockets 1..7
   - avoid INT stuck/race conditions
2. Implement VFS/BSD socket shim for TOE/lwIP fd routing:
   - TCP -> TOE
   - UDP unicast -> TOE
   - UDP multicast/broadcast -> lwIP
   - fallback to lwIP when TOE sockets exhausted
3. Validate behavior and performance:
   - DHCP/ping baseline
   - iperf TCP/UDP tx/rx
   - mixed scenario (lwIP multicast + TOE TCP)
   - long-run stability

## 6) Suggested start point for Claude
- Read these first:
  - `docs/WIZNET_TOE_CHANGELOG.md`
  - `docs/WIZNET_TOE_HANDOFF.md`
  - `main/ethernet_example_main.c`
  - `components/wiznet_toe/src/toe_dispatcher.c`
  - `components/wiznet_toe/src/wiznet_spi_port.c`
- Then implement shared INT ownership before adding full socket shim.
