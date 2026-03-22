/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sat.h
 * @brief SAT packet processing interface for the network CPU.
 *
 * Declares the entry point used by the network CPU to handle SAT IPC packets
 * received from the application CPU.
 */

#ifndef CPUNET_SAT_H
#define CPUNET_SAT_H

#include "sat_ipc.h"

/**
 * @brief Process an incoming SAT IPC packet.
 *
 * Dispatches the command carried by @p pkt to the appropriate SAT driver
 * operation (e.g., send, init, enable, disable).
 *
 * @param pkt Pointer to the base IPC packet received from the application CPU.
 *            The caller retains ownership; the pointer must remain valid for
 *            the duration of the call.
 * @return 0 on success, negative errno on failure.
 */
int hubble_sat_process(const struct sat_ipc_base *pkt);

#endif /* CPUNET_SAT_H */
