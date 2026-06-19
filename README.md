# FPSP_Camera_FPC

Camera (OV5640) bring-up firmware for an FPSP/FPC setup on the Raspberry Pi RP2350 (pico2 board).

## Build

This is a standard Raspberry Pi Pico SDK CMake project. Open it with the
**Raspberry Pi Pico** VS Code extension, or configure manually:

```sh
cmake -B build -G Ninja
cmake --build build
```

Target platform: `rp2350` / board `pico2` (set in `CMakeLists.txt`).

## Dependencies

This project depends on the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
(v2.2.0), licensed under BSD-3-Clause. The SDK is **not** included in this
repository. To build, install it via the Pico VS Code extension or set the
`PICO_SDK_PATH` environment variable to your local SDK checkout.

## Layout

- `main.c` — application entry point
- `camera_bringup_min/` — OV5640 driver and PIO heartbeat
- `CMakeLists.txt`, `pico_sdk_import.cmake` — build configuration
- `docs/` — project documentation
