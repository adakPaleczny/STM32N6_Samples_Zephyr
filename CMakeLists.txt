# SPDX-License-Identifier: Apache-2.0
# Root CMakeLists for the out-of-tree Zephyr module.
# Included by Zephyr build system via zephyr/module.yaml (cmake: .)

cmake_minimum_required(VERSION 3.20.0)

add_subdirectory_ifdef(CONFIG_SENSOR drivers)
