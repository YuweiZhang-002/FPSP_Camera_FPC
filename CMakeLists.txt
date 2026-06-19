# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13...3.27)

# === Single source of truth for target chip / board (Plan B) ===
# Hardcoded BEFORE pico_sdk_import so the SDK's toolchain selection picks the
# correct cortex-m33 / armv8-m.main flags on first configure.
# CACHE ... FORCE re-asserts the value on every configure, so a stale cache
# entry (e.g. from a previous rp2040 build) cannot win.
# Do NOT use `if(NOT DEFINED ...)` guards here: a stale CACHE value counts as
# defined and would silently keep the wrong target.
set(PICO_PLATFORM rp2350 CACHE STRING "Pico Platform" FORCE)
set(PICO_BOARD    pico2  CACHE STRING "Pico Board"    FORCE)

include(pico_sdk_import.cmake)

project(new_tests C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(camera_bringup_min)

add_executable(new_tests_app
    main.c
)

pico_generate_pio_header(new_tests_app
    ${CMAKE_CURRENT_LIST_DIR}/camera_bringup_min/heartbeat.pio
)

target_include_directories(new_tests_app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/camera_bringup_min
)

target_link_libraries(new_tests_app
    pico_stdlib
    hardware_pio
    camera_bringup_min
)

pico_enable_stdio_uart(new_tests_app 0)
pico_enable_stdio_usb(new_tests_app 1)

# Set the binary type to no_flash to run from RAM instead of Flash
pico_set_binary_type(new_tests_app no_flash)

pico_add_extra_outputs(new_tests_app)