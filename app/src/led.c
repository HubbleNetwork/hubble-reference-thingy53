/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define LED_INTERVAL_S   1

#if defined(CONFIG_APP_LED_GREEN)
#define LED_NODE DT_ALIAS(led1)
#elif defined(CONFIG_APP_LED_BLUE)
#define LED_NODE DT_ALIAS(led2)
#else
#define LED_NODE DT_ALIAS(led0)
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static void led_blink_cb(struct k_timer *timer)
{
	(void)gpio_pin_toggle_dt(&led);
}

static void led_stop_cb(struct k_timer *timer)
{
	(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

K_TIMER_DEFINE(led_timer, led_blink_cb, led_stop_cb);

int led_blink_start(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return -EAGAIN;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	k_timer_start(&led_timer, K_SECONDS(LED_INTERVAL_S), K_SECONDS(LED_INTERVAL_S));

	return 0;
}

void led_blink_stop(void)
{
	k_timer_stop(&led_timer);
}
