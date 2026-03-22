/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ipc/ipc_service.h>

#include <nrf53_cpunet_mgmt.h>

#include <hubble/hubble.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "ble.h"
#include "nvs.h"
#include "sat.h"
#include "shell.h"

/* sat ipc packet */
#include "sat_ipc.h"

#define IPC_TIMEOUT_S 3U

LOG_MODULE_REGISTER(main);

K_SEM_DEFINE(timer_sem, 0, 1);
K_SEM_DEFINE(init_sem, 0, 1);
K_SEM_DEFINE(key_sem, 0, 1);
K_SEM_DEFINE(bound_sem, 0, 1);
K_SEM_DEFINE(sat_sem, 0, 1);

K_MUTEX_DEFINE(radio_mutex);

static uint8_t _hubble_key[CONFIG_HUBBLE_KEY_SIZE];
static struct ipc_ept ep;
uint64_t utc_time;

/* Set a mark in a shared memory so the cpunet can
 * check which stack (BLE or Satellite) should be used
 * when boot.
 *
 * @_val: 1 for Bluetooth Stack
 *        0 for Satellite Stack
 */
#define _CPUNET_STACK_MARK(_val)                                               \
	do {                                                                   \
		*HUBBLE_SAT_MARK = _val;                                       \
		__DSB();                                                       \
		__ISB();                                                       \
	} while (0)

static void _ep_recv(const void *data, size_t len, void *priv)
{
	const struct sat_ipc_base *pkt = (const struct sat_ipc_base *)data;

	LOG_DBG("Received IPC data (%d)", len);

	if (pkt->cmd == SAT_CMD_OK) {
		k_sem_give(&sat_sem);
	}
}

int ipc_send(const uint8_t *data, size_t len)
{
	int ret = ipc_service_send(&ep, data, len);

	if (ret < 0) {
		return ret;
	}

	return k_sem_take(&sat_sem, K_SECONDS(IPC_TIMEOUT_S));
}

static void _ep_bound(void *priv)
{
	LOG_DBG("ipc bound");

	k_sem_give(&bound_sem);
}

static struct ipc_ept_cfg ep_cfg = {
	.name = "sat_ipc",
	.cb =
		{
			.bound = _ep_bound,
			.received = _ep_recv,
		},
};

const struct device *sat_ipc;

static int _sat_enable(void)
{
	int err;
	struct sat_ipc_base pkt = {
		.cmd = SAT_CMD_INIT,
	};

	LOG_INF("Run network core");

	_CPUNET_STACK_MARK(0);

	nrf53_cpunet_enable(true);

	sat_ipc = DEVICE_DT_GET(DT_NODELABEL(ipc1));

	err = ipc_service_open_instance(sat_ipc);
	if ((err < 0) && (err != -EALREADY)) {
		LOG_ERR("ipc_service_open_instance() failure");
		return err;
	}

	err = ipc_service_register_endpoint(sat_ipc, &ep, &ep_cfg);
	if (err < 0) {
		LOG_ERR("ipc_service_register_endpoint() failure");
		return err;
	}

	k_sem_take(&bound_sem, K_FOREVER);

	return ipc_send((const uint8_t *)&pkt, sizeof(pkt));
}

static int _sat_disable(void)
{
	int err;

	err = ipc_service_deregister_endpoint(&ep);
	if (err) {
		LOG_ERR("Deregistering HCI endpoint failed with: %d", err);
		return err;
	}

	err = ipc_service_close_instance(sat_ipc);
	if (err != 0) {
		LOG_ERR("Closing ipc service  failed with %d\n", err);
		return err;
	}

	nrf53_cpunet_enable(false);

	return 0;
}

int crypto_key_set(const char *key, size_t len)
{
	int ret;
	size_t olen;

	if (len != (ceil((double)CONFIG_HUBBLE_KEY_SIZE / 3) * 4)) {
		return -EINVAL;
	}

	ret = base64_decode(_hubble_key, sizeof(_hubble_key), &olen,
			    key, len);
	if (ret == 0) {
		(void)hubble_nvs_write(HUBBLE_KEY_ID, _hubble_key, sizeof(_hubble_key));
		k_sem_give(&key_sem);
	}

	return ret;
}

static int _key_get(void)
{
	if (hubble_nvs_read(HUBBLE_KEY_ID, _hubble_key, sizeof(_hubble_key)) == sizeof(_hubble_key)) {
		return 0;
	}

	return -ENOENT;
}

bool crypto_key_is_set(void)
{
	char _k[CONFIG_HUBBLE_KEY_SIZE] = {0};

	return memcmp(_hubble_key, _k, CONFIG_HUBBLE_KEY_SIZE) != 0;
}

int main(void)
{
	int err;

	if (_key_get() == 0) {
		k_sem_give(&key_sem);
	} else if (strlen(CONFIG_HUBBLE_DEVICE_KEY) != 0) {
		err = crypto_key_set(CONFIG_HUBBLE_DEVICE_KEY,
				     strlen(CONFIG_HUBBLE_DEVICE_KEY));
		if (err != 0) {
			LOG_ERR("Invalid key provided!");
		}
	}

	_CPUNET_STACK_MARK(1);

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	err = ble_init();
	if (err != 0) {
		goto end;
	}

	/* We sit here waiting to have UTC and KEY */
	k_sem_take(&init_sem, K_FOREVER);
	k_sem_take(&key_sem, K_FOREVER);

	/* Initialize utc time. */
	err = hubble_init(utc_time, _hubble_key);
	if (err != 0) {
		LOG_ERR("Failed to initialize Hubble BLE Network");
		return err;
	}

	while (true) {
		(void)sat_schedule_next_transmission();
		/**
		 * Here we are assuming that we are going to transmit to
		 * satellite at least once a day, otherwise we will not be able
		 * to synchronize with the BLE network.
		 */
		ble_enable(false);

		/* After that we will be transmitting to satellite */
		k_sem_take(&timer_sem, K_FOREVER);

		ble_disable(false);

		k_mutex_lock(&radio_mutex, K_FOREVER);
		err = _sat_enable();
		if (err < 0) {
			LOG_ERR("Failed to enable satellite IPC: %d", err);
			goto sat_cleanup;
		}

		err = sat_transmit();
		if (err < 0) {
			LOG_WRN("Failed to transmit packet");
		}

sat_cleanup:
		_sat_disable();
		k_mutex_unlock(&radio_mutex);
		_CPUNET_STACK_MARK(1);
	}
end:
	bt_disable();

	return err;
}
