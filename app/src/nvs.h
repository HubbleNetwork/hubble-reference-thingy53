/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file nvs.h
 * @brief Non-volatile storage (NVS) interface for Hubble persistent data.
 *
 * Provides typed read/write access to NVS entries identified by
 * @ref hubble_nvs_id values.
 */

#ifndef NVS_H
#define NVS_H

#include <inttypes.h>

/**
 * @brief Identifiers for Hubble NVS entries.
 */
enum hubble_nvs_id {
	HUBBLE_KEY_ID, /**< Hubble device key. */
};

/**
 * @brief Read data from non-volatile storage (NVS).
 *
 * This function reads data associated with a specific ID from NVS into
 * the provided buffer.
 *
 * @param id   The identifier for the data to be read.
 * @param data Pointer to the buffer where the read data will be stored.
 *             The buffer must be pre-allocated by the caller.
 * @param len  The length of the data to be read, in bytes.
 *             This ensures that the function does not read beyond the
 *             allocated buffer size.
 *
 * @return 0 on success, or a negative value on error.
 *         Possible errors include:
 *         - Invalid ID
 *         - Insufficient buffer size
 *         - Read failure
 */
int hubble_nvs_read(enum hubble_nvs_id id, uint8_t *data, size_t len);

/**
 * @brief Write data to non-volatile storage (NVS).
 *
 * This function writes data to NVS, associating it with a specific ID.
 *
 * @param id   The identifier for the data to be written.
 * @param data Pointer to the buffer containing the data to be written.
 * @param len  The length of the data to be written, in bytes.
 *
 * @return 0 on success, or a negative value on error.
 *         Possible errors include:
 *         - Invalid ID
 *         - Write failure
 *         - Insufficient storage space
 */
int hubble_nvs_write(enum hubble_nvs_id id, const uint8_t *data, size_t len);

#endif /* NVS_H */
