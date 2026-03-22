/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app.h
 * @brief Application-level interface for SAT transmission and sensor access.
 *
 * Provides functions to configure and trigger SAT transmissions, send raw IPC
 * messages, and read sensor data from the board.
 */

#ifndef APP_H
#define APP_H

#include <inttypes.h>
#include <stdlib.h>

#include <zephyr/drivers/sensor.h>


extern struct k_mutex radio_mutex;

/**
 * @brief Payload type for a SAT transmission.
 *
 * Selects the kind of data embedded in the SAT packet. Sensor-backed values
 * map directly to the corresponding Zephyr @c sensor_channel so the correct
 * channel is sampled automatically.
 */
enum sat_payload_type {
	CUSTOM,                                  /**< Arbitrary caller-supplied bytes. */
	GAUGE_VOLTAGE = SENSOR_CHAN_GAUGE_VOLTAGE, /**< Battery gauge voltage. */
	TEMP = SENSOR_CHAN_AMBIENT_TEMP,           /**< Ambient temperature. */
	PRESSURE = SENSOR_CHAN_PRESS,              /**< Atmospheric pressure. */
	HUMIDITY = SENSOR_CHAN_HUMIDITY,           /**< Relative humidity. */
	INVALID,                                 /**< Sentinel / error value. */
};

/**
 * @brief Send raw bytes over the inter-processor IPC channel.
 *
 * @param data Pointer to the data buffer to send.
 * @param len  Number of bytes to send.
 * @return 0 on success, negative errno on failure.
 */
int ipc_send(const uint8_t *data, size_t len);

/**
 * @brief Read a sensor value from the board.
 *
 * @param[out] data    Pointer to a @c sensor_value struct that receives the
 *                     measurement.
 * @param      channel Zephyr sensor channel to sample.
 * @return 0 on success, negative errno on failure.
 */
int sensor_data_get(struct sensor_value *data, enum sensor_channel channel);


/**
 * @brief Set the cryptographic key used for SAT payload encryption.
 *
 * @param key Pointer to the key material.
 * @param len Length of the key in bytes.
 * @return 0 on success, negative errno on failure.
 */
int crypto_key_set(const char *key, size_t len);

/**
 * @brief Check whether a cryptographic key has been configured.
 *
 * @return true if a key has been set, false otherwise.
 */
bool crypto_key_is_set(void);

#endif /* APP_H */
