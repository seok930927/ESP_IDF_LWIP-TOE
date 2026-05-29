# WIZNET TOE Integration Changelog

## 2026-05-29 (session 2): shared single-chip coexistence

### Problem analysis (single physical W5500, two drivers)
Identified 4 coexistence problems between esp_eth (MACRAW/socket0/lwIP) and
ioLibrary (TOE/sockets1..7):
1. Chip-reset race: `wizchip_init()` did a full `MR_RST`, wiping esp_eth's setup.
2. Buffer partition: esp_eth gives socket0 16KB and sockets1..7 0KB.
3. MACRAW duplicate-RX: socket0 copies TOE frames to lwIP -> potential RST.
4. SPI CS conflict: ioLibrary reconfigured the CS GPIO + manual toggle, breaking
   esp_eth's hardware CS on the shared pin.

### Fixes (this session)
- Problem 1: `wiznet_toe.c` no longer calls `wizchip_init()` (no second reset).
- Problem 2: `wiznet_toe_partition_sockets()` re-splits buffers 2KB x 8; called
  from `wiznet_toe_init()` which now runs between `example_eth_init()` and
  `esp_eth_start()` (i.e. after esp_eth `setup_default`, before socket-0 OPEN).
- Problem 4 (CS): single shared `spi_device` handle.
  - New `wiznet_spi_shared.c/.h`: esp_eth W5500 *custom SPI driver*
    (init/deinit/read/write) owns the one handle + a recursive lock.
  - `ethernet_init.c` injects it via `w5500_config.custom_spi_driver`.
  - `wiznet_spi_port.c` rewritten: ioLibrary byte/burst callbacks are coalesced
    into the same esp_eth-style transaction (cmd=16-bit addr, addr=8-bit ctrl);
    no own device, no CS GPIO. Lock taken in cris/cs callbacks.
  - Removed obsolete Kconfig: WIZNET_TOE_SPI_{HOST,CLOCK_MHZ,CS_GPIO,ACQUIRE_BUS}.
- Problem 3: not yet addressed (RX filter); needs the on-hardware overlap test.

### TOE data path (toward the goal)
- New `toe_socket.c/.h`: minimal TOE socket API (open/connect/listen/
  wait_connected/send/recv/close) over ioLibrary hardware TCP, sockets 1..7.
- New `toe_demo.c`: `CONFIG_WIZNET_TOE_ECHO_DEMO` test bed. Enabling it starts:
  - lwIP (MACRAW) TCP echo server on port 5000 (in main/ethernet_example_main.c);
  - TOE echo pool: hardware sockets 1..7 all listen on WIZNET_TOE_ECHO_PORT
    (5001) and echo, polled by one task (toe_demo.c);
  - a MACRAW RX hook (esp_eth_update_input_path) that prints "MACRAW recv" per
    frame then forwards to lwIP — to observe problem #3 (TOE traffic duplicated
    into the MACRAW/lwIP path).

### Build/verify (NOT yet built this session - user builds)
- Direction locked: COEXISTENCE driver (TOE + lwIP both usable), not pure TOE.

## 2026-05-29

### Step 1: Baseline netif split groundwork
- Added `components/wiznet_toe` component with ioLibrary source vendored under `components/wiznet_toe/lib/ioLibrary_Driver`.
- Added W5500 wiring config to `sdkconfig` (SCLK=8, MOSI=7, MISO=6, CS=44, INT=9, RST=43).
- Added `toe0` netif create/sync API in `components/wiznet_toe/src/toe_netif.c`.
- Hooked app flow in `main/ethernet_example_main.c` to:
  - initialize ioLibrary,
  - create `toe0`,
  - sync `eth0` DHCP IP to `toe0` and W5500 netinfo.

### Step 2: SPI coexistence hardening (in progress)
- Added Kconfig option `CONFIG_WIZNET_TOE_SPI_ACQUIRE_BUS` (default: y).
- Updated `wiznet_spi_port.c` so CS assert/dessert region can lock/unlock SPI bus (`spi_device_acquire_bus` / `spi_device_release_bus`).
- Goal: reduce race/interleave risk with other SPI traffic when sharing one SPI host.
- Persisted option in `sdkconfig` as `CONFIG_WIZNET_TOE_SPI_ACQUIRE_BUS=y`.

### Build status
- Current branch builds successfully after `toe0` integration.
- Remaining known warning source is 3rd-party ioLibrary code style warnings, treated as non-fatal for this component.
- Re-verified build after SPI bus lock changes:
  - `basic.bin` generated successfully.
  - App binary size: `0x5ff80` (63% partition free).

### Step 3: Handoff docs for next-phase implementation
- Added `docs/WIZNET_TOE_HANDOFF.md` with:
  - current integration snapshot,
  - next coding tasks,
  - known caveats,
  - validation matrix guidance.

### Step 4: TOE socket dispatcher (socket 1..7) added
- Added `components/wiznet_toe/src/toe_dispatcher.c`.
- Added public APIs in `components/wiznet_toe/include/wiznet_toe.h`:
  - `wiznet_toe_dispatcher_start()`
  - `wiznet_toe_dispatcher_stop()`
  - `wiznet_toe_register_socket_event_cb()`
- Dispatcher behavior:
  - polls `SIR` periodically,
  - handles only TOE sockets (1..7),
  - clears per-socket `Sn_IR` latched bits,
  - leaves socket 0 (MACRAW path) untouched.
- Added Kconfig options:
  - `CONFIG_WIZNET_TOE_DISPATCHER_ENABLE=y`
  - `CONFIG_WIZNET_TOE_DISPATCHER_PERIOD_MS=2`

### Step 5: Lifecycle integration
- `wiznet_toe_init()` now starts dispatcher when enabled.
- Optional app deinit flow in `main/ethernet_example_main.c` now calls `wiznet_toe_dispatcher_stop()`.

### Latest build status
- Build passed after dispatcher integration.
- `basic.bin` generated successfully.
- App binary size: `0x60140` (62% partition free).
