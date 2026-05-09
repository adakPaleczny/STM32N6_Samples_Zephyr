#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <lvgl.h>

#include "tof_display.h"
#include "NanoEdgeAI.h"

LOG_MODULE_REGISTER(tof_ai);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led    = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device       *tof_dev = DEVICE_DT_GET(DT_NODELABEL(vl53l5cx));

/* ── Shared AI result (ToF thread → display thread) ─────────────────────── */

static K_MUTEX_DEFINE(g_ai_lock);
static struct {
    int   id_class;
    float probabilities[NEAI_NUMBER_OF_CLASSES];
    bool  fresh;
} g_ai;

/* ── Thread stacks ───────────────────────────────────────────────────────── */

#define LED_STACK_SIZE     1024
#define TOF_STACK_SIZE     8192   /* extra headroom for NEAI inference */
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

/* ── ToF fetch + NEAI inference ──────────────────────────────────────────── */

static void tof_thread_func(void *p1, void *p2, void *p3)
{
    if (!device_is_ready(tof_dev)) {
        LOG_ERR("VL53L5CX device not ready");
        return;
    }

    float input_signal[NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER];
    float probabilities[NEAI_NUMBER_OF_CLASSES];
    int   id_class = 0;

    while (1) {
        if (sensor_sample_fetch(tof_dev) != 0) {
            LOG_ERR("ToF fetch failed");
            k_msleep(200);
            continue;
        }

        for (int z = 0; z < NEAI_INPUT_SIGNAL_LENGTH; z++) {
            struct sensor_value sv = {0};
            sensor_channel_get(tof_dev,
                               (enum sensor_channel)(SENSOR_CHAN_PRIV_START + z),
                               &sv);
            input_signal[z] = (float)sv.val1;
        }

        neai_classification(input_signal, probabilities, &id_class);

        k_mutex_lock(&g_ai_lock, K_FOREVER);
        g_ai.id_class = id_class;
        memcpy(g_ai.probabilities, probabilities, sizeof(probabilities));
        g_ai.fresh = true;
        k_mutex_unlock(&g_ai_lock);
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

    float probs_snap[NEAI_NUMBER_OF_CLASSES] = {0};
    int   class_snap = -1;

    while (1) {
        if (k_mutex_lock(&g_ai_lock, K_NO_WAIT) == 0) {
            if (g_ai.fresh) {
                class_snap = g_ai.id_class;
                memcpy(probs_snap, g_ai.probabilities, sizeof(probs_snap));
                g_ai.fresh = false;
                k_mutex_unlock(&g_ai_lock);

                tof_display_update_ai(class_snap, probs_snap);
            } else {
                k_mutex_unlock(&g_ai_lock);
            }
        }

        lv_timer_handler();
        k_msleep(16);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    }

    enum neai_state rc = neai_classification_init();
    if (rc != NEAI_OK) {
        LOG_ERR("neai_classification_init failed: %d", rc);
        return -1;
    }

    k_thread_create(&led_thread, led_stack, LED_STACK_SIZE,
                    led_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

    k_thread_create(&tof_thread, tof_stack, TOF_STACK_SIZE,
                    tof_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

    k_thread_create(&display_thread, display_stack, DISPLAY_STACK_SIZE,
                    display_thread_func, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

    return 0;
}
