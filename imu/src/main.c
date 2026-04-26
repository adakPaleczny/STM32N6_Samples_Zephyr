
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>

#include <stdarg.h>

#include "zephyr/drivers/gpio.h"
#include "zephyr/drivers/i2c.h"

#include "ism330dlc/ism330dlc.h"

LOG_MODULE_REGISTER(imu);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(csi_i2c));
static ISM330DLC_Object_t imu_obj;

#define IMU_I2C_ADDR_LOW   (ISM330DLC_I2C_ADD_L >> 1)
#define IMU_I2C_ADDR_HIGH  (ISM330DLC_I2C_ADD_H >> 1)

#define LED_STACK_SIZE 1024
#define IMU_STACK_SIZE 2048

K_THREAD_STACK_DEFINE(led_stack, LED_STACK_SIZE);
K_THREAD_STACK_DEFINE(imu_stack, IMU_STACK_SIZE);

static struct k_thread led_thread, imu_thread;


void led_thread_func(void *p1, void *p2, void *p3);
void imu_thread_func(void *p1, void *p2, void *p3);
static void hearbeat_init(void);
static int32_t imu_i2c_platform_init(void);
static int32_t imu_i2c_platform_deinit(void);
static int32_t imu_i2c_platform_get_tick(void);
static int32_t imu_i2c_platform_write(uint16_t addr, uint16_t reg, uint8_t *data, uint16_t len);
static int32_t imu_i2c_platform_read(uint16_t addr, uint16_t reg, uint8_t *data, uint16_t len);
static int32_t imu_try_init_at_address(uint8_t addr);
static int32_t imu_i2c_driver_init(void);
static int32_t imu_enable_sensors(void);
static int32_t imu_read_axes(ISM330DLC_Axes_t *acc, ISM330DLC_Axes_t *gyro);


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

static int32_t imu_i2c_platform_init(void)
{
    return device_is_ready(i2c_dev) ? ISM330DLC_OK : ISM330DLC_ERROR;
}

static int32_t imu_i2c_platform_deinit(void)
{
    return ISM330DLC_OK;
}

static int32_t imu_i2c_platform_get_tick(void)
{
    return (int32_t)k_uptime_get_32();
}

static int32_t imu_i2c_platform_write(uint16_t addr, uint16_t reg, uint8_t *data, uint16_t len)
{
    int ret = i2c_burst_write(i2c_dev, (uint16_t)(addr & 0x7F), (uint8_t)reg, data, len);

    return (ret == 0) ? ISM330DLC_OK : ISM330DLC_ERROR;
}

static int32_t imu_i2c_platform_read(uint16_t addr, uint16_t reg, uint8_t *data, uint16_t len)
{
    int ret = i2c_burst_read(i2c_dev, (uint16_t)(addr & 0x7F), (uint8_t)reg, data, len);

    return (ret == 0) ? ISM330DLC_OK : ISM330DLC_ERROR;
}

static int32_t imu_try_init_at_address(uint8_t addr)
{
    ISM330DLC_IO_t io_ctx = {
        .Init = imu_i2c_platform_init,
        .DeInit = imu_i2c_platform_deinit,
        .BusType = ISM330DLC_I2C_BUS,
        .Address = addr,
        .WriteReg = imu_i2c_platform_write,
        .ReadReg = imu_i2c_platform_read,
        .GetTick = imu_i2c_platform_get_tick,
    };
    uint8_t who_am_i = 0;

    if (ISM330DLC_RegisterBusIO(&imu_obj, &io_ctx) != ISM330DLC_OK) {
        return ISM330DLC_ERROR;
    }

    if (ISM330DLC_ReadID(&imu_obj, &who_am_i) != ISM330DLC_OK || who_am_i != ISM330DLC_ID) {
        return ISM330DLC_ERROR;
    }

    if (ISM330DLC_Init(&imu_obj) != ISM330DLC_OK) {
        return ISM330DLC_ERROR;
    }

    return ISM330DLC_OK;
}

static int32_t imu_i2c_driver_init(void)
{
    if (imu_try_init_at_address(IMU_I2C_ADDR_LOW) == ISM330DLC_OK) {
        LOG_INF("ISM330DLC initialized on 0x%02x", IMU_I2C_ADDR_LOW);
        return ISM330DLC_OK;
    }

    if (imu_try_init_at_address(IMU_I2C_ADDR_HIGH) == ISM330DLC_OK) {
        LOG_INF("ISM330DLC initialized on 0x%02x", IMU_I2C_ADDR_HIGH);
        return ISM330DLC_OK;
    }

    LOG_ERR("ISM330DLC initialization failed on 0x%02x and 0x%02x",
        IMU_I2C_ADDR_LOW, IMU_I2C_ADDR_HIGH);

    return ISM330DLC_ERROR;
}

static int32_t imu_enable_sensors(void)
{
    if (ISM330DLC_ACC_Enable(&imu_obj) != ISM330DLC_OK) {
        LOG_ERR("Failed to enable accelerometer");
        return ISM330DLC_ERROR;
    }

    if (ISM330DLC_GYRO_Enable(&imu_obj) != ISM330DLC_OK) {
        LOG_ERR("Failed to enable gyroscope");
        return ISM330DLC_ERROR;
    }

    return ISM330DLC_OK;
}

static int32_t imu_read_axes(ISM330DLC_Axes_t *acc, ISM330DLC_Axes_t *gyro)
{
    if (ISM330DLC_ACC_GetAxes(&imu_obj, acc) != ISM330DLC_OK) {
        return ISM330DLC_ERROR;
    }

    if (ISM330DLC_GYRO_GetAxes(&imu_obj, gyro) != ISM330DLC_OK) {
        return ISM330DLC_ERROR;
    }

    return ISM330DLC_OK;
}

void imu_thread_func(void *p1, void *p2, void *p3)
{
    ISM330DLC_Axes_t acc;
    ISM330DLC_Axes_t gyro;

    if (imu_i2c_driver_init() != ISM330DLC_OK) {
        return;
    }

    if (imu_enable_sensors() != ISM330DLC_OK) {
        return;
    }

    while (1) {
        if (imu_read_axes(&acc, &gyro) == ISM330DLC_OK) {
            LOG_INF("ACC[mg] x=%ld y=%ld z=%ld | GYRO[mdps] x=%ld y=%ld z=%ld",
                (long)acc.x, (long)acc.y, (long)acc.z,
                (long)gyro.x, (long)gyro.y, (long)gyro.z);
        } else {
            LOG_ERR("Failed to read IMU axes");
        }

        k_msleep(200);
    }
}
