/*
 * LVGL 4×4 distance-map visualization for the VL53L5CX ToF sensor.
 *
 * Layout on 800×480 display:
 *   - Header row (title + last-update stamp)
 *   - 4×4 grid of colored cells, each showing zone index + distance in mm
 *
 * Color coding (distance in mm):
 *   < 300 mm  → red    (very close / obstacle)
 *   < 800 mm  → orange
 *   < 1500 mm → yellow
 *   < 2500 mm → green
 *   < 4000 mm → blue
 *   ≥ 4000 mm → dark blue (very far / out of range)
 *   invalid   → dark gray
 */

#include "tof_display.h"

#include <stdio.h>
#include <lvgl.h>
#include <zephyr/drivers/display.h>

/* ── Layout constants ────────────────────────────────────────────────────── */

#define DISP_W      800
#define DISP_H      480
#define HEADER_H     48
#define FOOTER_H     32
#define GRID_PAD     12
#define CELL_GAP      6
#define COLS          8
#define ROWS          8

/*
 * Total grid area after subtracting header, footer, and outer padding.
 * Cell sizes are derived so the grid exactly fills the remaining space.
 */
#define GRID_W   (DISP_W - 2 * GRID_PAD)
#define GRID_H   (DISP_H - HEADER_H - FOOTER_H - 2 * GRID_PAD)
#define CELL_W   ((GRID_W - (COLS - 1) * CELL_GAP) / COLS)
#define CELL_H   ((GRID_H - (ROWS - 1) * CELL_GAP) / ROWS)

/* ── Module state ────────────────────────────────────────────────────────── */

static lv_obj_t *cells[ROWS][COLS];
static lv_obj_t *dist_labels[ROWS][COLS];
static lv_obj_t *title_label;
static lv_obj_t *status_label;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static lv_color_t dist_to_color(uint32_t mm, bool valid)
{
    if (!valid)      return lv_color_hex(0x3a3a4a);  /* gray   – no target  */
    if (mm < 300U)   return lv_color_hex(0xdd2222);  /* red    – very close */
    if (mm < 800U)   return lv_color_hex(0xff8800);  /* orange              */
    if (mm < 1500U)  return lv_color_hex(0xccb800);  /* yellow              */
    if (mm < 2500U)  return lv_color_hex(0x22aa44);  /* green               */
    if (mm < 4000U)  return lv_color_hex(0x2266cc);  /* blue   – far        */
    return lv_color_hex(0x1a2a44);                    /* dark blue – max     */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int tof_display_init(const struct device *disp_dev)
{
    /* Verify LVGL was initialised by Zephyr's SYS_INIT hook */
    if (lv_disp_get_default() == NULL) {
        return -ENODEV;
    }

    /* Ignore -ENOTSUP: some LTDC drivers don't implement set_pixel_format */
    (void)display_set_pixel_format(disp_dev, PIXEL_FORMAT_RGB_565);
    display_blanking_off(disp_dev);

    /* Screen background */
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* Title */
    title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "VL53L5CX  8x8  Distance  Map  (mm)");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xc0d0e0), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* 4×4 cell grid */
    int grid_x0 = GRID_PAD;
    int grid_y0 = HEADER_H + GRID_PAD;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int x = grid_x0 + c * (CELL_W + CELL_GAP);
            int y = grid_y0 + r * (CELL_H + CELL_GAP);

            /* Cell background */
            lv_obj_t *cell = lv_obj_create(scr);

            lv_obj_set_pos(cell, x, y);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_radius(cell, 8, LV_PART_MAIN);
            lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x3a3a4a), LV_PART_MAIN);
            lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            cells[r][c] = cell;

            /* Distance label (centered) */
            lv_obj_t *dlbl = lv_label_create(cell);

            lv_label_set_text(dlbl, "---");
            lv_obj_set_style_text_color(dlbl, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_text_font(dlbl, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_center(dlbl);
            dist_labels[r][c] = dlbl;
        }
    }

    /* Footer status bar */
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Press SW0 to save snapshot to SD card");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x80a0b0), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_timer_handler();
    return 0;
}

void tof_display_update(const uint32_t dist_mm[TOF_GRID_ZONES],
                        const uint8_t  statuses[TOF_GRID_ZONES])
{
    char buf[12];

    for (int i = 0; i < TOF_GRID_ZONES; i++) {
        int r     = i / COLS;
        int c     = i % COLS;
        bool valid = (statuses[i] == 0U) && (dist_mm[i] > 0U);

        lv_obj_set_style_bg_color(cells[r][c],
                                   dist_to_color(dist_mm[i], valid),
                                   LV_PART_MAIN);

        if (valid) {
            snprintf(buf, sizeof(buf), "%u", (unsigned)dist_mm[i]);
        } else {
            snprintf(buf, sizeof(buf), "---");
        }
        lv_label_set_text(dist_labels[r][c], buf);
    }
}

void tof_display_set_status(const char *msg)
{
    if (status_label) {
        lv_label_set_text(status_label, msg);
    }
}
