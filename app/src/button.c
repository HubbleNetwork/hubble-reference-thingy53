/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "ble.h"

#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec _button =
	GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback _button_cb_data;
static struct k_work _button_work;

static void _button_pressed_worker(struct k_work *work)
{
	if (!bt_is_ready()) {
		ble_enable(true);
	} else {
		ble_disable(true);
	}
}

static void _button_pressed(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	/* We are in ISR so let's delegate the task to a work queue. */
	k_work_submit(&_button_work);
}

int _button_init(void)
{
	int ret = 0;

	if (!gpio_is_ready_dt(&_button)) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&_button, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&_button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}

	gpio_init_callback(&_button_cb_data, _button_pressed, BIT(_button.pin));
	gpio_add_callback(_button.port, &_button_cb_data);

	k_work_init(&_button_work, _button_pressed_worker);

	return ret;
}

SYS_INIT(_button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
