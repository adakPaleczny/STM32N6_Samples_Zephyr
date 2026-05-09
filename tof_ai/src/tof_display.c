#include "tof_display.h"

#include <stdio.h>
#include <lvgl.h>
#include <zephyr/drivers/display.h>

/* ── Layout ──────────────────────────────────────────────────────────────── */

#define DISP_W       800
#define DISP_H       480
#define HEADER_H      48
#define PRED_Y       (HEADER_H + 6)
#define PRED_H       160
#define BARS_Y       (PRED_Y + PRED_H + 6)
#define BAR_GAP        4
#define BARS_TOTAL   (DISP_H - BARS_Y - 4)
#define BAR_H        ((BARS_TOTAL - (NEAI_NUMBER_OF_CLASSES - 1) * BAR_GAP) \
                      / NEAI_NUMBER_OF_CLASSES)
#define LABEL_COL_W  100
#define PCT_COL_W     64
#define BAR_PAD        4
#define BAR_X        (LABEL_COL_W + BAR_PAD)
#define BAR_W        (DISP_W - BAR_X - PCT_COL_W - BAR_PAD)

/* ── Per-class accent colors ─────────────────────────────────────────────── */

static const uint32_t CLASS_COLORS[NEAI_NUMBER_OF_CLASSES] = {
    0x22aa44,  /* OK       – green   */
    0xdd2222,  /* STOP     – red     */
    0x2266cc,  /* THUMB_UP – blue    */
    0x8833aa,  /* VICTORIA – purple  */
    0xff6600,  /* FUCK     – orange  */
    0x556677,  /* OTHER    – slate   */
};

/* ── Module state ────────────────────────────────────────────────────────── */

static lv_obj_t *pred_box;
static lv_obj_t *pred_class_label;
static lv_obj_t *pred_conf_label;
static lv_obj_t *bar_fills[NEAI_NUMBER_OF_CLASSES];
static lv_obj_t *pct_labels[NEAI_NUMBER_OF_CLASSES];

/* ── Public API ──────────────────────────────────────────────────────────── */

int tof_display_init(const struct device *disp_dev)
{
    if (lv_disp_get_default() == NULL) {
        return -ENODEV;
    }

    (void)display_set_pixel_format(disp_dev, PIXEL_FORMAT_RGB_565);
    display_blanking_off(disp_dev);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "VL53L5CX  ToF  AI  Classification");
    lv_obj_set_style_text_color(title, lv_color_hex(0xc0d0e0), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* Prediction box */
    pred_box = lv_obj_create(scr);
    lv_obj_set_pos(pred_box, 0, PRED_Y);
    lv_obj_set_size(pred_box, DISP_W, PRED_H);
    lv_obj_set_style_radius(pred_box, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(pred_box, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pred_box, lv_color_hex(0x556677), LV_PART_MAIN);
    lv_obj_set_style_pad_all(pred_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(pred_box, LV_OBJ_FLAG_SCROLLABLE);

    pred_class_label = lv_label_create(pred_box);
    lv_label_set_text(pred_class_label, "---");
    lv_obj_set_style_text_color(pred_class_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(pred_class_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(pred_class_label, LV_ALIGN_CENTER, 0, -14);

    pred_conf_label = lv_label_create(pred_box);
    lv_label_set_text(pred_conf_label, "");
    lv_obj_set_style_text_color(pred_conf_label, lv_color_hex(0xe0e8f0), LV_PART_MAIN);
    lv_obj_set_style_text_font(pred_conf_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(pred_conf_label, LV_ALIGN_CENTER, 0, 22);

    /* Probability bars */
    for (int i = 0; i < NEAI_NUMBER_OF_CLASSES; i++) {
        int y = BARS_Y + i * (BAR_H + BAR_GAP);

        /* Class name label */
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, neai_get_class_name(i));
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xc0d0e0), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(lbl, BAR_PAD, y + (BAR_H - 14) / 2);

        /* Bar background */
        lv_obj_t *bar_bg = lv_obj_create(scr);
        lv_obj_set_pos(bar_bg, BAR_X, y);
        lv_obj_set_size(bar_bg, BAR_W, BAR_H);
        lv_obj_set_style_radius(bar_bg, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x1e2a38), LV_PART_MAIN);
        lv_obj_set_style_pad_all(bar_bg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

        /* Bar fill */
        lv_obj_t *fill = lv_obj_create(bar_bg);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 1, BAR_H);
        lv_obj_set_style_radius(fill, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(fill, lv_color_hex(CLASS_COLORS[i]), LV_PART_MAIN);
        lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        bar_fills[i] = fill;

        /* Percentage label */
        lv_obj_t *pct = lv_label_create(scr);
        lv_label_set_text(pct, "0.0%");
        lv_obj_set_style_text_color(pct, lv_color_hex(0x90a8c0), LV_PART_MAIN);
        lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(pct, BAR_X + BAR_W + BAR_PAD, y + (BAR_H - 14) / 2);
        pct_labels[i] = pct;
    }

    lv_timer_handler();
    return 0;
}

void tof_display_update_ai(int id_class,
                            const float probabilities[NEAI_NUMBER_OF_CLASSES])
{
    /* Prediction box */
    bool valid = (id_class >= 0 && id_class < NEAI_NUMBER_OF_CLASSES);
    uint32_t col = valid ? CLASS_COLORS[id_class] : 0x556677U;
    lv_obj_set_style_bg_color(pred_box, lv_color_hex(col), LV_PART_MAIN);
    lv_label_set_text(pred_class_label,
                      valid ? neai_get_class_name(id_class) : "---");

    char buf[32];
    if (valid) {
        snprintf(buf, sizeof(buf), "Confidence: %.1f%%",
                 probabilities[id_class] * 100.0f);
        lv_label_set_text(pred_conf_label, buf);
    } else {
        lv_label_set_text(pred_conf_label, "");
    }

    /* Probability bars */
    for (int i = 0; i < NEAI_NUMBER_OF_CLASSES; i++) {
        int fill_w = (int)(probabilities[i] * (float)BAR_W);
        lv_obj_set_width(bar_fills[i], fill_w > 1 ? fill_w : 1);

        snprintf(buf, sizeof(buf), "%.1f%%", probabilities[i] * 100.0f);
        lv_label_set_text(pct_labels[i], buf);
    }
}
