
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <rfid/rc522.h>

LOG_MODULE_REGISTER(rfid_app);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static const struct device *rc522_dev = DEVICE_DT_GET(DT_NODELABEL(rc522));

#define LED_STACK_SIZE  1024
#define RFID_STACK_SIZE 2048

K_THREAD_STACK_DEFINE(led_stack,  LED_STACK_SIZE);
K_THREAD_STACK_DEFINE(rfid_stack, RFID_STACK_SIZE);

static struct k_thread led_thread;
static struct k_thread rfid_thread;

static void led_thread_func(void *p1, void *p2, void *p3);
static void rfid_thread_func(void *p1, void *p2, void *p3);
static void heartbeat_init(void);

int main(void)
{
	heartbeat_init();

	k_thread_create(&led_thread, led_stack, LED_STACK_SIZE,
		led_thread_func, NULL, NULL, NULL,
		K_PRIO_COOP(6), 0, K_NO_WAIT);

	k_thread_create(&rfid_thread, rfid_stack, RFID_STACK_SIZE,
		rfid_thread_func, NULL, NULL, NULL,
		K_PRIO_COOP(7), 0, K_NO_WAIT);

	return 0;
}

static void led_thread_func(void *p1, void *p2, void *p3)
{
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(500);
	}
}

static void heartbeat_init(void)
{
	if (!device_is_ready(led.port)) {
		return;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
}

static void rfid_thread_func(void *p1, void *p2, void *p3)
{
	if (!device_is_ready(rc522_dev)) {
		LOG_ERR("RC522 device not ready");
		return;
	}

	LOG_INF("RC522 ready — waiting for card...");

	while (1) {
		if (rc522_detect_card(rc522_dev) == 0) {
			struct rc522_uid uid;

			if (rc522_get_uid(rc522_dev, &uid) == 0) {
				LOG_INF("Card UID: %02X:%02X:%02X:%02X",
					uid.data[0], uid.data[1],
					uid.data[2], uid.data[3]);
			} else {
				LOG_WRN("Card detected but UID read failed");
			}

			rc522_halt_card(rc522_dev);
			/* Debounce: wait before polling again */
			k_msleep(500);
		}

		k_msleep(50);
	}
}
