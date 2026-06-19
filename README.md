# FPSP_Camera_FPC

This repo contains FPC schematics of the newly developed FPSP camera project on an FPC board, aiming for super-high-speed parallel image processing.

## Repository Structure
| Directory | Use |
| --------- | --- |
| archive_old/ | Archived initial design (**not working**) |
| custom_fp/ | Footprint from external libraries |
| docs/ | Documentation gathered from external sources |
| camera_bringup_min/ | RP2350 firmware: OV5640 driver and PIO heartbeat |

## TODO
- [x] RP2350 Power Supply Layout
- [ ] Add Proper Power Sequencing
- [ ] Source RP2354 A4
- [ ] Source B2B Connector
- [x] B2B Connector Pinout + Layout
- [x] HSTX Layout


## 12/12/2025 PCB ordering notes

- order FR-4 board with SMT assembly, run thru **functionality tests**
- order FPC without SMT assembly, test the **FPC characteristics and stiffener position**
- RP2345A and ICM45686 IMU ordered separately to test the functionality. Once it passes the prototype test, order at JLCPCB via **global sourcing** and prep for next assembly

### TODO
 - [x] Place JLCPCB order
 - [ ] functionality test, FR4 prototype
 - [ ] functionality test, FPC stiffener position
------ 
 - [ ] sourcing RP2345A
 - [ ] sourcing ICM45686
 - [ ] sourcing board-to-board connectors from Molex 

## Firmware

Camera (OV5640) bring-up firmware for the Raspberry Pi RP2350 (pico2 board).

### Build

Standard Raspberry Pi Pico SDK CMake project. Open it with the
**Raspberry Pi Pico** VS Code extension, or configure manually:

```sh
cmake -B build -G Ninja
cmake --build build
```

Target platform: `rp2350` / board `pico2` (set in `CMakeLists.txt`).

Key files: `main.c`, `camera_bringup_min/` (OV5640 driver + PIO heartbeat),
`CMakeLists.txt`, `pico_sdk_import.cmake`.

### Firmware dependencies

Depends on the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
(v2.2.0), licensed under BSD-3-Clause. The SDK is **not** included in this
repository — install it via the Pico VS Code extension or set the
`PICO_SDK_PATH` environment variable to your local SDK checkout.
