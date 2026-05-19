/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <hubble/hubble.h>
#include <hubble/ble.h>

#include "app.h"
#include "ble.h"
#include "sat.h"

LOG_MODULE_REGISTER(ble);

#define HUBBLE_BLE_UUID_CONNECTABLE 0xFCA7
#define HUBBLE_BLE_BUFFER_LEN       31

#define SPBLE_PKT_CMD               0x02
#define SPBLE_CMD_SETUTC            0x02
#define SPBLE_CMD_SETEPHEMERIS      0x07

#define SVC_UUID                                                               \
	BT_UUID_128_ENCODE(0xef2dc9a1, 0x40be, 0x44b6, 0x9dda, 0x8a00fcd61dc0)
#define CHR_UUID                                                               \
	BT_UUID_128_ENCODE(0xef2dc9a1, 0x40be, 0x44b6, 0x9dda, 0x8a00fcd61dc1)

static bool ble_off_by_user;

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(SVC_UUID);
static const struct bt_uuid_128 chr_uuid = BT_UUID_INIT_128(CHR_UUID);

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct bt_data app_ad[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(HUBBLE_BLE_UUID_CONNECTABLE)),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      BT_UUID_16_ENCODE(HUBBLE_BLE_UUID_CONNECTABLE)),
};
static uint8_t app_ad_buffer[HUBBLE_BLE_BUFFER_LEN];

extern struct k_sem init_sem;
extern uint64_t utc_time;

int ble_init(void);

static void _ble_work_handler(struct k_work *work)
{
	ble_adv_stop();

	ble_adv_start();
}

K_WORK_DEFINE(_ble_work, _ble_work_handler);

static void _timer_ble_cb(struct k_timer *timer)
{
	k_work_submit(&_ble_work);
}

K_TIMER_DEFINE(ble_timer, _timer_ble_cb, NULL);

static void _adv_provisioning_work_handler(struct k_work *work)
{
	(void)ble_init();
}

K_WORK_DEFINE(_provisioning_work, _adv_provisioning_work_handler);

int ble_adv_start(void)
{
	int err;
	size_t out_len;

	k_timer_start(&ble_timer, K_SECONDS(CONFIG_HUBBLE_SAMPLE_ADV_FREQUENCY),
		      K_NO_WAIT);

	out_len = ARRAY_SIZE(app_ad_buffer);
	err = hubble_ble_advertise_get(NULL, 0, app_ad_buffer, &out_len);
	if (err != 0) {
		return err;
	}

	app_ad[0] = (struct bt_data)BT_DATA_BYTES(
		BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(HUBBLE_BLE_UUID));
	app_ad[1].data_len = out_len;
	app_ad[1].type = BT_DATA_SVC_DATA16;
	app_ad[1].data = app_ad_buffer;

	return bt_le_adv_start(
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_NRPA, BT_GAP_ADV_SLOW_INT_MIN,
				BT_GAP_ADV_SLOW_INT_MAX, NULL),
		app_ad, ARRAY_SIZE(app_ad), NULL, 0);
}

void ble_adv_stop(void)
{
	bt_le_adv_stop();
	k_timer_stop(&ble_timer);
}

static void _connected_cb(struct bt_conn *conn, uint8_t err)
{
	k_timer_stop(&ble_timer);
}

static void _disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	if (utc_time != 0) {
		k_sem_give(&init_sem);
	} else {
		k_work_submit(&_provisioning_work);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = _connected_cb,
	.disconnected = _disconnected_cb,
};

int ble_init(void)
{
	return bt_le_adv_start(
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_NRPA | BT_LE_ADV_OPT_CONN,
				BT_GAP_ADV_FAST_INT_MIN_2,
				BT_GAP_ADV_FAST_INT_MAX_2, NULL),
		app_ad, 2, sd, ARRAY_SIZE(sd));
}

static ssize_t _chr_write_cb(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, const void *data,
			     uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *req = (uint8_t *)data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (req[0] == SPBLE_PKT_CMD) {
		uint8_t cmd = req[1];

		switch (cmd) {
		case SPBLE_CMD_SETUTC:
			if (len != 10) {
				return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
			} else {
				memcpy(&utc_time, req + 2, sizeof(utc_time));
				hubble_time_set(utc_time);
				utc_time = utc_time - k_uptime_get();
			}
			break;
		case SPBLE_CMD_SETEPHEMERIS:
			if (len > 2 + sizeof(uint32_t)) {
				uint32_t count = 0;
				size_t entry_size = sizeof(uint32_t) + sizeof(uint16_t);
				uint8_t *p = (req + 2) + sizeof(uint32_t);
				size_t available = len - 2 - sizeof(uint32_t);

				memcpy(&count, (req + 2), sizeof(count));

				for (uint32_t i = 0; i < count; i++) {
					if (available < entry_size) {
						break;
					}

					int resp = sat_pass_add(
						*(uint32_t *)p,
						*(uint16_t *)(p +
							      sizeof(uint32_t)));
					if (resp != 0) {
						break;
					}

					p += entry_size;
					available -= entry_size;
				}
			}
			break;
		default:
			break;
		}
	}

	return len;
}

void ble_enable(bool user_requested)
{
	int err;

	k_mutex_lock(&radio_mutex, K_FOREVER);

	if (!user_requested && ble_off_by_user) {
		goto end;
	}

	if (user_requested) {
		ble_off_by_user = false;
	}

	err = bt_enable(NULL);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		goto end;
	}

	/* Time is not synced */
	if (utc_time == 0) {
		err = ble_init();
		if (err < 0) {
			LOG_ERR("Failed to advertise time sync: %d", err);
		}
	} else {
		err = ble_adv_start();
		if (err < 0) {
			LOG_ERR("Failed to get the advertisement data: %d", err);
		}
	}

end:
	k_mutex_unlock(&radio_mutex);
}

void ble_disable(bool user_requested)
{
	k_mutex_lock(&radio_mutex, K_FOREVER);

	if (user_requested) {
		ble_off_by_user = true;
	}

	/* Stop the timer and disable BLE */
	ble_adv_stop();
	bt_disable();

	k_mutex_unlock(&radio_mutex);
}

/* Check for BT_GATT_PERM_WRITE_LESC permissions */
BT_GATT_SERVICE_DEFINE(
	vnd_svc, BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&chr_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP |
				       BT_GATT_CHRC_EXT_PROP,
			       BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE,
			       NULL, _chr_write_cb, NULL), );
