
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(imu);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(ism330dlc));

#define LED_STACK_SIZE 1024
#define IMU_STACK_SIZE 2048

K_THREAD_STACK_DEFINE(led_stack, LED_STACK_SIZE);
K_THREAD_STACK_DEFINE(imu_stack, IMU_STACK_SIZE);

static struct k_thread led_thread, imu_thread;

void led_thread_func(void *p1, void *p2, void *p3);
void imu_thread_func(void *p1, void *p2, void *p3);
static void hearbeat_init(void);


int main(){
    hearbeat_init();

    k_thread_create(&led_thread, led_stack, LED_STACK_SIZE,
        (k_thread_entry_t)led_thread_func, NULL, NULL, NULL,
        K_PRIO_COOP(6), 0, K_NO_WAIT);

        k_thread_create(&imu_thread, imu_stack, IMU_STACK_SIZE,
        (k_thread_entry_t)imu_thread_func, NULL, NULL, NULL,
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

void imu_thread_func(void *p1, void *p2, void *p3)
{
    struct sensor_value acc[3];
    struct sensor_value gyro[3];

    if (!device_is_ready(imu_dev)) {
        LOG_ERR("ISM330DLC device not ready");
        return;
    }

    while (1) {
        if (sensor_sample_fetch(imu_dev) == 0) {
            sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, acc);
            sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);

            LOG_INF("ACC [m/s²]  x=%d.%06d  y=%d.%06d  z=%d.%06d",
                acc[0].val1, abs(acc[0].val2),
                acc[1].val1, abs(acc[1].val2),
                acc[2].val1, abs(acc[2].val2));

            LOG_INF("GYRO[rad/s] x=%d.%06d  y=%d.%06d  z=%d.%06d",
                gyro[0].val1, abs(gyro[0].val2),
                gyro[1].val1, abs(gyro[1].val2),
                gyro[2].val1, abs(gyro[2].val2));
        } else {
            LOG_ERR("sensor_sample_fetch failed");
        }

        k_msleep(200);
    }
}
