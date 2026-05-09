cmake_minimum_required(VERSION 3.20.0)

set(BOARD stm32n6570_dk)
set(SHIELD st_b_cams_imx_mb1854)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/tof_display.c
    # src/neai_shim.c
)

target_link_libraries(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/libneai.a)
