#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <lvgl.h>

#include "tof_display.h"
#include "sd_logger.h"

LOG_MODULE_REGISTER(tof);

#define LED0_NODE DT_ALIAS(led0)
#define SW0_NODE  DT_ALIAS(sw0)

static const struct gpio_dt_spec led    = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec btn    = GPIO_DT_SPEC_GET(SW0_NODE,  gpios);
static const struct device       *tof_dev = DEVICE_DT_GET(DT_NODELABEL(vl53l5cx));

static struct gpio_callback btn_cb_data;

/* ── Shared ToF data (sensor → display thread) ───────────────────────────── */

static K_SEM_DEFINE(save_sem, 0, 1);
static K_MUTEX_DEFINE(g_tof_lock);
static struct {
    uint32_t dist_mm[TOF_GRID_ZONES];
    uint8_t  status[TOF_GRID_ZONES];
    bool     fresh;
} g_tof;

static void btn_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&save_sem);
}

/* ── Thread stacks ───────────────────────────────────────────────────────── */

#define LED_STACK_SIZE     1024
#define TOF_STACK_SIZE     4096
#define DISPLAY_STACK_SIZE 32768

K_THREAD_STACK_DEFINE(led_stack,     LED_STACK_SIZE);
K_THREAD_STACK_DEFINE(tof_stack,     TOF_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);

static struct k_thread led_thread, tof_thread, display_thread;

/* ── LED heartbeat ───────────────────────────────────────────────────────── */

static void led_thread_func(void *p1, void *p2, void *p3)
{
    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }
}

/* ── Sensor polling ──────────────────────────────────────────────────────── */

static void tof_thread_func(void *p1, void *p2, void *p3)
{
    if (!device_is_ready(tof_dev)) {
        LOG_ERR("VL53L5CX device not ready");
        return;
    }

    while (1) {
        if (sensor_sample_fetch(tof_dev) != 0) {
            LOG_ERR("ToF fetch failed");
            k_msleep(200);
            continue;
        }

        k_mutex_lock(&g_tof_lock, K_FOREVER);

        for (int z = 0; z < TOF_GRID_ZONES; z++) {
            struct sensor_value sv = {0};

            sensor_channel_get(tof_dev,
                               (enum sensor_channel)(SENSOR_CHAN_PRIV_START + z),
                               &sv);
            g_tof.dist_mm[z] = (uint32_t)sv.val1;
            g_tof.status[z]  = (uint8_t)sv.val2;
        }
        g_tof.fresh = true;

        k_mutex_unlock(&g_tof_lock);
    }
}

/* ── Display / LVGL ──────────────────────────────────────────────────────── */

static void display_thread_func(void *p1, void *p2, void *p3)
{
    const struct device *disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(disp_dev)) {
        LOG_ERR("Display not ready");
        return;
    }

    if (tof_display_init(disp_dev) != 0) {
        LOG_ERR("Display init failed");
        return;
    }

    uint32_t dist_snap[TOF_GRID_ZONES];
    uint8_t  stat_snap[TOF_GRID_ZONES];
    char     status_buf[64];

    while (1) {
        if (k_mutex_lock(&g_tof_lock, K_NO_WAIT) == 0) {
            if (g_tof.fresh) {
                memcpy(dist_snap, g_tof.dist_mm, sizeof(dist_snap));
                memcpy(stat_snap, g_tof.status,  sizeof(stat_snap));
                g_tof.fresh = false;
                k_mutex_unlock(&g_tof_lock);

                tof_display_update(dist_snap, stat_snap);
            } else {
                k_mutex_unlock(&g_tof_lock);
            }
        }

        if (k_sem_take(&save_sem, K_NO_WAIT) == 0) {
            tof_display_set_status("Saving to SD...");
            lv_timer_handler();

            int rc = sd_logger_save(dist_snap, stat_snap);
            if (rc == 0) {
                snprintf(status_buf, sizeof(status_buf),
                         "Saved at %u ms  (SW0 to save again)",
                         (unsigned)k_uptime_get_32());
            } else {
                snprintf(status_buf, sizeof(status_buf),
                         "SD save failed: %d", rc);
            }
            tof_display_set_status(status_buf);
        }

        lv_timer_handler();
        k_msleep(16); /* ~60 Hz LVGL refresh */
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    }

    if (device_is_ready(btn.port)) {
        gpio_pin_configure_dt(&btn, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&btn_cb_data, btn_pressed, BIT(btn.pin));
        gpio_add_callback(btn.port, &btn_cb_data);
    } else {
        LOG_WRN("SW0 button not ready");
    }

    k_thread_create(&led_thread, led_stack, LED_STACK_SIZE,
                    led_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

    k_thread_create(&tof_thread, tof_stack, TOF_STACK_SIZE,
                    tof_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

    /*
     * Display thread runs at higher priority (lower number) so LVGL stays
     * responsive even while the ToF thread busy-polls the sensor.
     */
    k_thread_create(&display_thread, display_stack, DISPLAY_STACK_SIZE,
                    display_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

    return 0;
}
