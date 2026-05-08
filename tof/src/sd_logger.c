#include "sd_logger.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <ff.h>

LOG_MODULE_REGISTER(sd_logger);

#define MOUNT_POINT   "/SD:"
#define DIR_PATH      MOUNT_POINT "/tof/dataset"
#define CSV_PATH      DIR_PATH "/data.csv"

static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type      = FS_FATFS,
    .fs_data   = &fat_fs,
    .mnt_point = MOUNT_POINT,
};

static bool mounted;

static int ensure_mounted(void)
{
    if (mounted) {
        return 0;
    }
    int rc = fs_mount(&mp);
    if (rc != 0) {
        LOG_ERR("fs_mount failed: %d", rc);
        return rc;
    }
    mounted = true;

    /* Create directory tree (ignore EEXIST) */
    rc = fs_mkdir(MOUNT_POINT "/tof");
    if (rc != 0 && rc != -EEXIST) {
        LOG_ERR("mkdir /tof failed: %d", rc);
        return rc;
    }
    rc = fs_mkdir(DIR_PATH);
    if (rc != 0 && rc != -EEXIST) {
        LOG_ERR("mkdir dataset failed: %d", rc);
        return rc;
    }
    return 0;
}

int sd_logger_save(const uint32_t dist_mm[TOF_GRID_ZONES],
                   const uint8_t  statuses[TOF_GRID_ZONES])
{
    int rc = ensure_mounted();
    if (rc != 0) {
        return rc;
    }

    struct fs_file_t f;
    fs_file_t_init(&f);

    struct fs_dirent entry;
    bool file_exists = (fs_stat(CSV_PATH, &entry) == 0);

    if (file_exists) {
        rc = fs_open(&f, CSV_PATH, FS_O_APPEND | FS_O_WRITE);
    } else {
        rc = fs_open(&f, CSV_PATH, FS_O_CREATE | FS_O_WRITE);
    }
    if (rc != 0) {
        LOG_ERR("fs_open failed: %d", rc);
        return rc;
    }

    char line[512];
    int  len;

    if (!file_exists) {
        len = snprintf(line, sizeof(line),
                       "timestamp_ms");
        for (int z = 0; z < TOF_GRID_ZONES; z++) {
            len += snprintf(line + len, sizeof(line) - len,
                            ",z%d_mm,z%d_status", z, z);
        }
        len += snprintf(line + len, sizeof(line) - len, "\n");
        fs_write(&f, line, len);
    }

    len = snprintf(line, sizeof(line), "%u", (unsigned)k_uptime_get_32());
    for (int z = 0; z < TOF_GRID_ZONES; z++) {
        len += snprintf(line + len, sizeof(line) - len,
                        ",%u,%u",
                        (unsigned)dist_mm[z],
                        (unsigned)statuses[z]);
    }
    len += snprintf(line + len, sizeof(line) - len, "\n");

    ssize_t written = fs_write(&f, line, len);
    fs_close(&f);

    if (written < 0) {
        LOG_ERR("fs_write failed: %d", (int)written);
        return (int)written;
    }

    LOG_INF("Saved row to %s (%d bytes)", CSV_PATH, len);
    return 0;
}
