/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

int sensor_data_get(struct sensor_value *data, enum sensor_channel channel)
{
	static const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme680);
	int err = 0;

	if (!device_is_ready(dev)) {
		return -EAGAIN;
	}

	err = sensor_sample_fetch(dev);
	if (err == 0) {
		err = sensor_channel_get(dev, channel, data);
	}

	return err;
}
