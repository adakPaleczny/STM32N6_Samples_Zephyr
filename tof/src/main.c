
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(tof);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *tof_dev  = DEVICE_DT_GET(DT_NODELABEL(vl53l5cx));

#define LED_STACK_SIZE 1024
#define TOF_STACK_SIZE 4096   /* VL53L5CX data structs are sizeable */

K_THREAD_STACK_DEFINE(led_stack, LED_STACK_SIZE);
K_THREAD_STACK_DEFINE(tof_stack, TOF_STACK_SIZE);

static struct k_thread led_thread, tof_thread;

void led_thread_func(void *p1, void *p2, void *p3);
void tof_thread_func(void *p1, void *p2, void *p3);
static void hearbeat_init(void);

int main(){
    hearbeat_init();

    k_thread_create(&led_thread, led_stack, LED_STACK_SIZE,
        (k_thread_entry_t)led_thread_func, NULL, NULL, NULL,
        K_PRIO_COOP(6), 0, K_NO_WAIT);

    k_thread_create(&tof_thread, tof_stack, TOF_STACK_SIZE,
        (k_thread_entry_t)tof_thread_func, NULL, NULL, NULL,
        K_PRIO_COOP(7), 0, K_NO_WAIT);

    return 0;
}

void led_thread_func(void *p1, void *p2, void *p3)
{
    int ret;
    bool led_state = true;

    while (1) {
        ret = gpio_pin_toggle_dt(&led);
        if (ret < 0) {
            return;
        }

        led_state = !led_state;
        k_msleep(100);
    }
}

static void hearbeat_init(void)
{
    if (!device_is_ready(led.port)) {
        return;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
}

void tof_thread_func(void *p1, void *p2, void *p3)
{
    struct sensor_value dist;

    if (!device_is_ready(tof_dev)) {
        LOG_ERR("VL53L5CX device not ready");
        return;
    }

    while (1) {
        if (sensor_sample_fetch(tof_dev) == 0 &&
            sensor_channel_get(tof_dev, SENSOR_CHAN_DISTANCE, &dist) == 0) {
            LOG_INF("Zone 0 dist: %d.%03d m",
                dist.val1, dist.val2 / 1000);
        } else {
            LOG_ERR("ToF read failed");
        }

        /* VL53L5CX ranging frequency set to 5 Hz → poll at 200 ms */
        k_msleep(200);
    }
}
