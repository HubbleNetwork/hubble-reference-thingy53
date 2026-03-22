/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "app.h"
#include "ble.h"
#include "sat.h"

#define NUMBER_OF_TIMESTAMP_TO_PRINT 4

K_MUTEX_DEFINE(_mutex);

static int _cmd_retransmission(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t freq;

	if (argc != 2) {
		shell_print(sh, "It requires a value between 0 and 10");
		return -EINVAL;
	}

	freq = strtol(argv[1], NULL, 10);
	if ((freq < 0) || (freq > 10)) {
		shell_print(sh, "It requires a value between 0 and 10");
		return -EINVAL;
	}

	k_mutex_lock(&_mutex, K_FOREVER);
	sat_retransmission_set(freq);
	k_mutex_unlock(&_mutex);

	return 0;
}

static enum sensor_channel _sensor_channel_get(const char *str)
{
	if (strncmp(str, "temperature", sizeof("temperature")) == 0) {
		return SENSOR_CHAN_AMBIENT_TEMP;
	} else if (strncmp(str, "pressure", sizeof("pressure")) == 0) {
		return SENSOR_CHAN_PRESS;
	} else if (strncmp(str, "humidity", sizeof("humidity")) == 0) {
		return SENSOR_CHAN_HUMIDITY;
	} else if (strncmp(str, "battery", sizeof("battery")) == 0) {
		return SENSOR_CHAN_GAUGE_VOLTAGE;
	}

	return SENSOR_CHAN_MAX;
}

static int _cmd_payload(const struct shell *sh, size_t argc, char **argv)
{
	int ret = -EINVAL;

	k_mutex_lock(&_mutex, K_FOREVER);

	if (argc == 2) {
		enum sensor_channel channel = _sensor_channel_get(argv[1]);
		if (channel != SENSOR_CHAN_MAX) {
			ret = 0;
		}
		sat_payload_set(channel, NULL, 0);
	} else if ((argc == 3) &&
		   (memcmp("custom", argv[1], strlen("custom")) == 0)) {
		size_t len = strlen(argv[2]);
		if (len < 13) {
			sat_payload_set(CUSTOM, argv[2], len);
			ret = 0;
		}
	}

	k_mutex_unlock(&_mutex);

	if (ret != 0) {
		shell_print(sh, "Invalid payload %s", argv[1]);
	}

	return 0;
}

static int _cmd_sensor(const struct shell *sh, size_t argc, char **argv)
{
	int ret = -EINVAL;
	struct sensor_value data;
	enum sensor_channel chan;

	if (argc == 2) {
		chan = _sensor_channel_get(argv[1]);

		ret = sensor_data_get(&data, chan);

		if (ret == 0) {
			shell_print(sh, "Sensor %s value %d.%06d", argv[1],
				    data.val1, data.val2);
		} else {
			shell_print(sh, "Error %s - %d", argv[1], ret);
		}
	}

	return ret;
}

static int _cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	extern uint64_t utc_time;
	int ret = 0;
	uint16_t len;
	uint32_t timestamps[NUMBER_OF_TIMESTAMP_TO_PRINT];

#ifdef CONFIG_FUEL_GAUGE
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(fuel_gauge));
	fuel_gauge_prop_t poll_props[] = {
		FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE,
		FUEL_GAUGE_VOLTAGE,
	};
	union fuel_gauge_prop_val poll_vals[ARRAY_SIZE(poll_props)];
#endif

	if (utc_time == 0) {
		shell_print(sh, "Waiting for time synchronization");
		goto end;
	}

	if (!crypto_key_is_set()) {
		shell_print(sh, "Waiting for hubble cryptographic key");
		goto end;
	}

	if (bt_is_ready()) {
		shell_print(sh, "Beaconing");
	} else if (sat_is_transmitting()) {
		shell_print(sh, "Transmitting to satellite");
	}

	len = ble_adv_next_timestamp_get(timestamps, NUMBER_OF_TIMESTAMP_TO_PRINT);

	if (len > 0) {
		shell_print(sh, "Next scheduled transmissions:");
	}

	for (uint16_t i = 0; i < len; i++) {
		struct tm t;
		time_t ts = (time_t)timestamps[i];

		gmtime_r(&ts, &t);
		shell_print(sh, "\t%02d/%02d/%04d %02d:%02d:%02d UTC (%d)",
			    t.tm_mon + 1, t.tm_mday, t.tm_year + 1900,
			    t.tm_hour, t.tm_min, t.tm_sec, timestamps[i]);
	}


end:
#ifdef CONFIG_FUEL_GAUGE
	ret = fuel_gauge_get_props(dev, poll_props, poll_vals,
				   ARRAY_SIZE(poll_props));

	if (ret == 0) {
		shell_print(sh, "Battery - Charge: %d%%, Voltage: %dmV",
			    poll_vals[0].relative_state_of_charge,
			    poll_vals[1].voltage / 1000);
	}
#endif

	return ret;
}

static int _cmd_key(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(sh, "Use: key <base64-hubble-key>");
		return -EINVAL;
	}

	if (crypto_key_set(argv[1], strlen(argv[1])) != 0) {
		shell_print(sh, "Invalid key provided");
		return -EINVAL;
	}

	return 0;
}

static int _cmd_ble(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(sh, "Use: bluetooth <disable|enable>");
		return -EINVAL;
	}

	if (strncmp(argv[1], "enable", sizeof("enable")) == 0) {
		ble_enable(true);
	} else if (strncmp(argv[1], "disable", sizeof("disable")) == 0) {
		ble_disable(true);
	} else {
		shell_print(sh, "Invalid parameter %s. Use: bluetooth <disable|enable>",
		            argv[1]);
		return -EINVAL;
	}

	return 0;
}

SHELL_CMD_REGISTER(
	retransmission, NULL,
	"Set retransmission frequency (in seconds). Available options: [0-10]",
	_cmd_retransmission);
SHELL_CMD_REGISTER(
	key, NULL,
	"Set a cryptography key (in base64)",
	_cmd_key);
SHELL_CMD_REGISTER(
	payload, NULL,
	"Set payload to transmit. Available options: temperature, pressure, "
	"humidity, battery or custom (sequence of (up to) 12 chars)",
	_cmd_payload);
SHELL_CMD_REGISTER(
	status, NULL,
	"Read device status (beaconing / transmitting sat / waiting)",
	_cmd_status);
SHELL_CMD_REGISTER(
	sensor,
	NULL, "Read sensor data. Available options: temperature, pressure or humidity",
	_cmd_sensor);
SHELL_CMD_REGISTER(
	bluetooth,
	NULL, "Disable or enable Bluetooth. Available options are enable and disable",
	_cmd_ble);
