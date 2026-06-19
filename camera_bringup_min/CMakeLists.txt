# camera_bringup_min is a reusable driver layer.
# The executable entry point lives in the top-level CMakeLists.txt.

add_library(camera_bringup_min STATIC
    ov5640.c
)

target_include_directories(camera_bringup_min PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(camera_bringup_min PUBLIC
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_pwm
    hardware_clocks
)
