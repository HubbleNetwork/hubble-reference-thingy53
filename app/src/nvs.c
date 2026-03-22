/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>

#include "nvs.h"


#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   PARTITION_SIZE(NVS_PARTITION)

#define KEY_ID               1

static struct nvs_fs fs;

int _nvs_init(void)
{
	int err;
	struct flash_pages_info info;

	fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		return -1;
	}

	fs.offset = NVS_PARTITION_OFFSET;
	err = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (err != 0) {
		return -1;
	}

	fs.sector_size = info.size;
	fs.sector_count = NVS_PARTITION_SIZE / fs.sector_size;

	err = nvs_mount(&fs);
	if (err != 0) {
		;
		return -1;
	}

	return 0;
}

int hubble_nvs_read(enum hubble_nvs_id id, uint8_t *data, size_t len)
{
	return nvs_read(&fs, id, data, len);
}

int hubble_nvs_write(enum hubble_nvs_id id, const uint8_t *data, size_t len)
{
	(void)nvs_delete(&fs, id);
	return nvs_write(&fs, id, data, len);
}

/* Just using a priority lower than FLASH_INIT_PRIORITY */
SYS_INIT(_nvs_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
