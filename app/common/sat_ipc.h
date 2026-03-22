/*
 * Copyright (c) 2025 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sat_ipc.h
 * @brief Shared SAT inter-processor communication (IPC) definitions.
 *
 * Defines the command codes, packet structures, and shared-memory constants
 * used by both the application CPU and the network CPU to exchange SAT
 * transmission requests over the IPC channel.
 */

#ifndef COMMON_SAT_IPC_H
#define COMMON_SAT_IPC_H

#include <hubble/sat.h>
#include <hubble/sat/packet.h>

/** @brief Maximum number of data bytes in a SAT IPC packet payload. */
#define HUBBLE_SAT_PACKET_DATA_LEN 16

/**
 * @brief Address of the shared-memory synchronisation mark.
 *
 * A volatile 32-bit location in the shared RAM region used as a handshake
 * flag between the two CPUs.
 */
#define HUBBLE_SAT_MARK            (volatile uint32_t *)0x2007F000

/**
 * @brief Commands exchanged between the application CPU and the network CPU.
 *
 * Written into @ref sat_ipc_base::cmd to indicate the requested operation or
 * the outcome of a previous request.
 */
enum sat_ipc_command {
	SAT_CMD_SEND,    /**< Request a SAT packet transmission. */
	SAT_CMD_INIT,    /**< Initialise the SAT radio driver. */
	SAT_CMD_ENABLE,  /**< Enable the SAT radio. */
	SAT_CMD_DISABLE, /**< Disable the SAT radio. */
	SAT_CMD_OK,      /**< Acknowledgement: previous command succeeded. */
	SAT_CMD_FAIL,    /**< Acknowledgement: previous command failed. */
};

/**
 * @brief Base header present in every SAT IPC message.
 *
 * All SAT IPC packet types begin with this structure so that the receiving
 * side can dispatch on the command before casting to the full packet type.
 */
struct sat_ipc_base {
	enum sat_ipc_command cmd; /**< Command identifier. */
};

/**
 * @brief Full SAT IPC packet carrying a SAT frame for transmission.
 *
 * Used with @c SAT_CMD_SEND to transfer the pre-built SAT frame from the
 * application CPU to the network CPU.
 */
struct sat_ipc_packet {
	struct sat_ipc_base base;               /**< Common IPC header. */
	struct hubble_sat_packet_frames packet; /**< SAT frame payload. */
};

#endif /* COMMON_SAT_IPC_H */
