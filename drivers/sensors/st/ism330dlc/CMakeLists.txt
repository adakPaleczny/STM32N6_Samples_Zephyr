# ST Microelectronics ISM330DLC 6-axis IMU sensor driver
#
# Copyright (c) 2020 STMicroelectronics
#
# SPDX-License-Identifier: Apache-2.0
#
zephyr_library()

zephyr_library_sources(ism330dlc.c)
zephyr_library_sources(ism330dlc_i2c.c)
zephyr_library_sources(ism330dlc_reg.c)

# local headers: ism330dlc.h, ism330dlc_reg.h
zephyr_library_include_directories(.)