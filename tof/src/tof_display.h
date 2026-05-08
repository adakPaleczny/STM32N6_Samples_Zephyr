#pragma once

#include <zephyr/device.h>
#include <stdint.h>

#define TOF_GRID_ZONES 64   /* 8×8 */

int  tof_display_init(const struct device *disp_dev);
void tof_display_update(const uint32_t dist_mm[TOF_GRID_ZONES],
                        const uint8_t  statuses[TOF_GRID_ZONES]);
void tof_display_set_status(const char *msg);
