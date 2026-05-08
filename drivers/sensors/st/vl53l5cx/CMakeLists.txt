# ST Microelectronics VL53L5CX multi-zone ToF sensor driver
#
# Copyright (c) 2025 STMicroelectronics
# SPDX-License-Identifier: Apache-2.0
#
# Place the following files in this directory before building:
#   BSP:  vl53l5cx.c  vl53l5cx.h
#   ULD:  vl53l5cx_api.c  vl53l5cx_api.h
#         vl53l5cx_plugin_xtalk.c  vl53l5cx_plugin_xtalk.h
#         vl53l5cx_plugin_detection_thresholds.c  vl53l5cx_plugin_detection_thresholds.h
#         vl53l5cx_platform.h  (platform-specific types, from ULD package)

zephyr_library()

zephyr_library_sources(vl53l5cx_zephyr.c)
zephyr_library_sources(vl53l5cx.c)
zephyr_library_sources(vl53l5cx_api.c)
zephyr_library_sources(platform.c)
zephyr_library_sources(vl53l5cx_plugin_xtalk.c)
zephyr_library_sources(vl53l5cx_plugin_detection_thresholds.c)

# Local headers: vl53l5cx.h, vl53l5cx_api.h, vl53l5cx_platform.h, plugins
zephyr_library_include_directories(.)
