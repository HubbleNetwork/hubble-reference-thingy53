/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file shell.h
 * @brief Shell-facing accessors for SAT IPC state.
 *
 * Provides functions used by the debug shell to inspect the current SAT IPC
 * packet and retransmission configuration.
 */

#ifndef SHELL_H
#define SHELL_H

#include <inttypes.h>

#include "sat_ipc.h"

/**
 * @brief Retrieve the current SAT IPC packet.
 *
 * Copies the most recently configured SAT IPC packet into @p pkt so the
 * shell can inspect or display its contents.
 *
 * @param[out] pkt Pointer to a @ref sat_ipc_packet struct that receives the
 *                 current packet data.
 */
void hubble_sat_ipc_packet_get(struct sat_ipc_packet *pkt);

/**
 * @brief Get the current SAT retransmission count.
 *
 * @return The number of retransmissions configured for the next SAT
 *         transmission.
 */
uint8_t hubble_sat_retrans_get(void);

#endif /* SHELL_H */
