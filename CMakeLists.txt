# SPDX-License-Identifier: Apache-2.0
# Root CMakeLists for the out-of-tree Zephyr module.
# Included by Zephyr build system via zephyr/module.yaml (cmake: .)

cmake_minimum_required(VERSION 3.20.0)

zephyr_include_directories(include)

if(CONFIG_SENSOR OR CONFIG_RC522)
  add_subdirectory(drivers)
endif()
