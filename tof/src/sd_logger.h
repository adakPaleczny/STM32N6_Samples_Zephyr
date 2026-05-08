#pragma once

#include <stdint.h>
#include "tof_display.h"   /* TOF_GRID_ZONES */

/*
 * Append one row to /SD:/tof/dataset/data.csv.
 * Mounts the filesystem on first call. Creates the directory tree if needed.
 * Writes a CSV header on first call (or when the file did not exist).
 *
 * Returns 0 on success, negative errno on failure.
 */
int sd_logger_save(const uint32_t dist_mm[TOF_GRID_ZONES],
                   const uint8_t  statuses[TOF_GRID_ZONES]);
