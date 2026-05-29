# WIZNET TOE Handoff Notes

## Current integrated state
- W5500 wiring has been applied in sdkconfig:
  - SCLK=GPIO8, MOSI=GPIO7, MISO=GPIO6, CS=GPIO44, INT=GPIO9, RST=GPIO43
- ioLibrary component is vendored under `components/wiznet_toe/lib/ioLibrary_Driver`.
- `toe0` netif metadata path exists:
  - create `toe0`
  - sync eth0 DHCP IP to `toe0`
  - sync same IP/GW/MASK to W5500 netinfo
- SPI transaction window now supports bus locking while CS is asserted.
- TOE dispatcher for socket 1..7 is implemented and started by `wiznet_toe_init()`.
- Dispatcher clears TOE `Sn_IR` bits and supports optional callback registration.

## Files to inspect first
- `main/ethernet_example_main.c`
- `components/wiznet_toe/src/toe_netif.c`
- `components/wiznet_toe/src/wiznet_spi_port.c`
- `components/wiznet_toe/Kconfig`
- `docs/WIZNET_TOE_CHANGELOG.md`

## Next implementation tasks
1. SPI/INT integration with esp_eth W5500 path
- Goal: prevent races between MACRAW path and TOE path for one physical W5500 chip.
- Implement shared ISR dispatch policy for socket 0 (MACRAW) and socket 1..7 (TOE).
- Validate IR clear ownership to avoid stuck INT line.
- Note: current dispatcher is polling-based and intentionally does not touch socket 0.

2. TOE socket/VFS shim
- Implement TOE fd table and VFS ops (`read/write/ioctl/close`).
- Add routing policy function:
  - TCP -> TOE
  - UDP unicast -> TOE
  - UDP multicast/broadcast -> lwIP
- Keep fallback to lwIP when TOE sockets are exhausted.

3. Validation matrix
- DHCP + ping baseline
- TCP iperf tx/rx
- UDP iperf tx/rx
- Mixed run: lwIP multicast + TOE tcp simultaneously
- long run stability

## Known caveats
- ioLibrary third-party code emits compile warnings in this toolchain; warnings are non-fatal for the wiznet_toe component.
- ioLibrary defines `MR`, which clashes with xtensa register macro names in some translation units; currently warning only.
