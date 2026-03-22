#include "sat.h"

#include <zephyr/kernel.h>
#include <hubble/sat.h>
#include <hubble/sat/packet.h>

extern int hubble_sat_board_init(void);
extern int hubble_sat_board_enable(void);
extern int hubble_sat_board_disable(void);
extern int hubble_sat_board_packet_send(
	const struct hubble_sat_packet_frames *packet);

int hubble_sat_process(const struct sat_ipc_base *pkt)
{
	int err = 0;

	switch (pkt->cmd) {
	case SAT_CMD_SEND:
		err = hubble_sat_board_packet_send(
			&((struct sat_ipc_packet *)pkt)->packet);
		break;
	case SAT_CMD_INIT:
		err = hubble_sat_board_init();
		break;
	case SAT_CMD_ENABLE:
		err = hubble_sat_board_enable();
		break;
	case SAT_CMD_DISABLE:
		err = hubble_sat_board_disable();
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}
