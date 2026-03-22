/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ble.h
 * @brief Bluetooth LE advertising interface.
 *
 * Provides functions to initialise the BLE stack, control advertising, and
 * query timing information used to synchronise SAT transmissions with BLE
 * advertisement slots.
 */

#ifndef BLE_H
#define BLE_H

#include <zephyr/bluetooth/uuid.h>

/**
 * @brief Initialise the Bluetooth LE subsystem.
 *
 * Registers GATT services, sets up the advertising data, and prepares the
 * controller for use. Must be called once before any other BLE function.
 *
 * @return 0 on success, negative errno on failure.
 */
int ble_init(void);

/**
 * @brief Start Bluetooth LE advertising.
 *
 * Begins broadcasting advertisement packets. @ref ble_init must have been
 * called successfully before invoking this function.
 *
 * @return 0 on success, negative errno on failure.
 */
int ble_adv_start(void);

/**
 * @brief Stop Bluetooth LE advertising.
 *
 * Halts advertisement broadcasting. Safe to call even if advertising is
 * already stopped.
 */
void ble_adv_stop(void);

/**
 * @brief Get next timestamps for sat transmissions.
 *
 * Scans @p timestamps and returns the index of the earliest timestamp that
 * falls after the current time, used to select the next available BLE
 * advertisement slot.
 *
 * @param timestamps Array of candidate timestamps (absolute, in milliseconds).
 * @param len        Number of entries in @p timestamps.
 * @return Index of the next upcoming timestamp, or @p len if none are found.
 */
uint16_t ble_adv_next_timestamp_get(uint32_t *timestamps, uint16_t len);

void ble_enable(bool user_requested);

void ble_disable(bool user_requested);

#endif /* BLE_H */
