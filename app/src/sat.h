/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sat.h
 * @brief SAT transmission control interface.
 *
 * Provides functions to configure and drive SAT transmissions: scheduling
 * the next window, tuning RF power and retransmission cadence, setting the
 * payload, and triggering the actual over-the-air burst.
 *
 * Typical call sequence:
 *   1. sat_pass_add() — populate the transmission schedule
 *      from satellite pass data received over BLE.
 *   2. sat_payload_set()             — choose what data to embed in each packet.
 *   3. sat_schedule_next_transmission() — arm the kernel timer for the next window.
 *   4. sat_transmit()                — called from the timer callback to send.
 */

#ifndef SAT_H
#define SAT_H

/**
 * @brief Arm the kernel timer for the next scheduled transmission window.
 *
 * Walks the pre-loaded timestamp table (populated via
 * sat_pass_add()) to find the earliest future satellite pass,
 * then starts a one-shot timer that fires when that window opens.
 *
 * @return 0 on success, -1 when no future window is available.
 */
int sat_schedule_next_transmission(void);

/**
 * @brief Set the SAT retransmission interval.
 *
 * Within a single transmission window the packet is sent repeatedly at this
 * cadence until the window closes.
 *
 * @param retransmission_s Interval between retransmissions, in seconds.
 * @return 0 on success, negative errno on failure.
 */
int sat_retransmission_set(uint8_t retransmission_s);

/**
 * @brief Set the SAT transmit power.
 *
 * @param power Transmit power level in dBm.
 * @return 0 on success, negative errno on failure.
 */
int sat_power_set(int8_t power);

/**
 * @brief Set the payload for the next SAT transmission.
 *
 * For sensor-backed types (TEMP, PRESSURE, HUMIDITY, GAUGE_VOLTAGE) @p data
 * may be NULL and @p len zero — the driver is sampled automatically at
 * transmit time. For CUSTOM, @p data and @p len must describe the raw bytes
 * to embed.
 *
 * @param type Payload type; see @ref sat_payload_type.
 * @param data Pointer to payload bytes, or NULL for sensor-backed types.
 * @param len  Length of @p data in bytes, or 0 for sensor-backed types.
 * @return 0 on success, negative errno on failure.
 */
int sat_payload_set(enum sat_payload_type type, const uint8_t *data, size_t len);

/**
 * @brief Register a satellite pass window with the scheduler.
 *
 * Each call appends one entry to the internal timestamp table.  The table
 * holds up to 300 entries; calls beyond that return -ENOSPC.  Entries are
 * consumed in order by sat_schedule_next_transmission().
 *
 * @param timestamp UTC epoch second at which the pass window opens.
 * @param duration  Length of the pass window in seconds.
 * @return 0 on success, -ENOSPC when the table is full.
 */
int sat_pass_add(uint32_t timestamp, uint16_t duration);

/**
 * @brief Trigger a SAT transmission with the currently configured payload.
 *
 * Builds a Hubble SAT packet from the active payload, then retransmits it
 * repeatedly (at the configured interval) for the duration of the current
 * pass window.  Blocks until the window closes.
 *
 * @return 0 on success, negative errno on failure.
 */
int sat_transmit(void);

/**
 * @brief Query whether a SAT transmission is currently in progress.
 *
 * @return true while sat_transmit() is executing, false otherwise.
 */
bool sat_is_transmitting(void);

#endif
