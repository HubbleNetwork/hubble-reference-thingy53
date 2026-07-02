/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>

#include <hubble/sat.h>

#include "sat_ipc.h"
#include "app.h"
#include "ble.h"
#include "led.h"
#include "sat.h"

LOG_MODULE_REGISTER(sat);

#define HUBBLE_NUMBER_OF_TIMESTAMPS 300

#define HUBBLE_FOR_WHILE(_period)                                              \
	for (int64_t _now = k_uptime_get(), _end = k_uptime_get() + (_period); \
	     _now <= _end; _now = k_uptime_get())

static int8_t _power = 0; /* Default is 0dbm the max for nrf53. */
static uint8_t _retransmission_frequency_s =
	CONFIG_HUBBLE_RETRANSMISSION_FREQUENCY;

static uint8_t _custom_payload_buffer[HUBBLE_SAT_PAYLOAD_MAX];
static uint8_t _custom_payload_size;
static enum sat_payload_type _payload_type = INVALID;
static bool _transmitting;
extern struct k_sem timer_sem;
extern uint64_t utc_time;


#ifndef CONFIG_HUBBLE_SAMPLE_DEBUG
static uint16_t sat_passes_idx;
static uint16_t sat_passes_count;
static struct _sat_pass {
	uint32_t timestamp;
	uint16_t duration;
} __packed sat_passes[HUBBLE_NUMBER_OF_TIMESTAMPS] = {0};
#endif

int sat_retransmission_set(uint8_t retransmission_s)
{
	_retransmission_frequency_s = retransmission_s;

	return 0;
}

int sat_power_set(int8_t power)
{
	_power = power;

	return 0;
}

int sat_payload_set(enum sat_payload_type type, const uint8_t *data, size_t len)
{
	_payload_type = type;

	if (len != 0) {
		memcpy(_custom_payload_buffer, data, len);
		_custom_payload_size = len;
	}

	return 0;
}

int hubble_sat_board_init(void)
{
	/**
	 * Do nothing here since we need to initialize
	 * the board everytime with switch stacks in the
	 * network core
	 */

	return 0;
}

int hubble_sat_board_enable(void)
{
	struct sat_ipc_base pkt = {
		.cmd = SAT_CMD_ENABLE,
	};

	LOG_DBG("sat board enable");

	ipc_send((const uint8_t *)&pkt, sizeof(pkt));

	return 0;
}

int hubble_sat_board_disable(void)
{
	struct sat_ipc_base pkt = {
		.cmd = SAT_CMD_DISABLE,
	};

	LOG_DBG("sat board disable");
	ipc_send((const uint8_t *)&pkt, sizeof(pkt));

	return 0;
}

int hubble_sat_board_packet_send(const struct hubble_sat_packet_frames *packet)
{
	struct sat_ipc_packet pkt = {
		.base =
			{
				.cmd = SAT_CMD_SEND,
			},
	};

	memcpy(&pkt.packet, packet, sizeof(*packet));
	ipc_send((const uint8_t *)&pkt, sizeof(pkt));

	return 0;
}

static uint8_t _channel_to_hubble(int channel)
{
	uint8_t ret = 0;

	switch (channel) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		ret = 1;
		break;
	case SENSOR_CHAN_PRESS:
		ret = 2;
		break;
	case SENSOR_CHAN_HUMIDITY:
		ret = 3;
		break;
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		ret = 4;
		break;
	default:
		break;
	}

	return ret;
}

bool sat_is_transmitting(void)
{
	return _transmitting;
}

static int sat_transmission_time_get(void)
{
#ifndef CONFIG_HUBBLE_SAMPLE_DEBUG
	if (sat_passes_idx >= sat_passes_count) {
		return -1;
	}

	return sat_passes[sat_passes_idx].duration;
#else
	return 120;
#endif
}

static size_t _nearest_packet_len_get(size_t pkt_len)
{
	if (pkt_len == 0) {
		return 0U;
	} else if (pkt_len < 4U) {
		return 4U;
	} else if (pkt_len < 9U) {
		return 9U;
	} else if (pkt_len < 13U) {
		return 13U;
	}

	return pkt_len;
}

int sat_transmit(void)
{
	int ret;
	struct hubble_sat_packet pkt;
	uint8_t pkt_buffer[HUBBLE_SAT_PAYLOAD_MAX] = {0};
	size_t pkt_len = 0;

#ifdef CONFIG_FUEL_GAUGE
	struct sensor_value sensor_data;
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(fuel_gauge));

	pkt_buffer[0] = _channel_to_hubble(_payload_type);

	if (_payload_type == CUSTOM) {
		pkt_len = HUBBLE_SAT_PAYLOAD_MAX;
		memcpy(&pkt_buffer[1], _custom_payload_buffer,
		       _custom_payload_size);
	} else {
		if (_payload_type == GAUGE_VOLTAGE) {
			fuel_gauge_prop_t poll_props =
				FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE;
			union fuel_gauge_prop_val poll_val;

			ret = fuel_gauge_get_props(dev, &poll_props, &poll_val,
						   1);
			if (ret == 0) {
				pkt_len =
					sizeof(poll_val.relative_state_of_charge) +
					1;
				memcpy(&pkt_buffer[1],
				       &poll_val.relative_state_of_charge,
				       sizeof(poll_val.relative_state_of_charge));
			}
		} else {
			ret = sensor_data_get(&sensor_data, _payload_type);
			if (ret == 0) {
				pkt_len = sizeof(sensor_data) + 1;
				memcpy(&pkt_buffer[1], &sensor_data,
				       sizeof(sensor_data));
			}
		}
	}
#endif
	pkt_len = _nearest_packet_len_get(pkt_len);

	ret = hubble_sat_packet_get(&pkt, pkt_buffer, pkt_len);
	if (ret != 0) {
		return ret;
	}

	_transmitting = true;
	(void)led_blink_start();

	HUBBLE_FOR_WHILE(sat_transmission_time_get() * 1000)
	{
		ret = hubble_sat_packet_send(&pkt, HUBBLE_SAT_RELIABILITY_NONE);
		if (ret != 0) {
			return ret;
		}
		k_sleep(K_SECONDS(_retransmission_frequency_s));
	}

	(void)led_blink_stop();
	_transmitting = false;

	return 0;
}

static void _timer_cb(struct k_timer *timer)
{
	k_sem_give(&timer_sem);
}

K_TIMER_DEFINE(sat_timer, _timer_cb, NULL);

int sat_pass_add(uint32_t timestamp, uint16_t duration)
{
#ifndef CONFIG_HUBBLE_SAMPLE_DEBUG
	if (sat_passes_count >= HUBBLE_NUMBER_OF_TIMESTAMPS) {
		return -ENOSPC;
	}

	sat_passes[sat_passes_count].timestamp = timestamp;
	sat_passes[sat_passes_count].duration = duration;

	sat_passes_count++;
#endif

	return 0;
}

int sat_schedule_next_transmission(void)
{
	uint32_t timeout;

#ifndef CONFIG_HUBBLE_SAMPLE_DEBUG
	uint32_t now = (uint32_t)((utc_time + k_uptime_get()) / 1000);

	if (sat_passes_idx >= sat_passes_count) {
		return -1;
	}

	while (sat_passes[sat_passes_idx].timestamp < now + 1) {
		sat_passes_idx++;
		if (sat_passes_idx >= sat_passes_count) {
			return -1;
		}
	}

	timeout = sat_passes[sat_passes_idx].timestamp - now;
#else
	timeout = 60;
#endif

	k_timer_start(&sat_timer, K_SECONDS(timeout), K_NO_WAIT);

	return 0;
}

uint16_t ble_adv_next_timestamp_get(uint32_t *timestamps, uint16_t len)
{
	uint16_t number_elements = MIN(len, (sat_passes_count - sat_passes_idx));

	for (uint16_t i = 0; i < number_elements; i++) {
		timestamps[i] = sat_passes[sat_passes_idx + i].timestamp;
	}

	return number_elements;
}
