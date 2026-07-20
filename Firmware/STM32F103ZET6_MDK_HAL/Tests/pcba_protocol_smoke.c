#include <stdint.h>
#include <stdio.h>

#include "app_protocol.h"

int main(void)
{
    PcbaFrame ack = {0};
    PcbaFrame pressure = {0};
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    uint32_t pressure_001mmhg = 0u;
    size_t frame_len;

    frame_len = PcbaProtocol_BuildPressure(PCBA_CMD_SYNC_PRESSURE_CAL,
                                           0u,
                                           145042u,
                                           frame,
                                           sizeof(frame));
    if (frame_len != 12u ||
        frame[6] != 0x92u || frame[7] != 0x36u ||
        frame[8] != 0x02u || frame[9] != 0x00u) {
        fputs("calibration pressure was not encoded as 0.001mmHg\n", stderr);
        return 1;
    }

    pressure.len = 4u;
    pressure.data[0] = 0x41u;
    pressure.data[1] = 0x7Cu;
    pressure.data[2] = 0x2Au;
    pressure.data[3] = 0x00u;
    if (!PcbaProtocol_GetPressure001mmHg(&pressure, &pressure_001mmhg) ||
        pressure_001mmhg != 2784321u) {
        fputs("pressure query was not decoded as 0.001mmHg\n", stderr);
        return 6;
    }
    pressure.data[0] = 0xFFu;
    pressure.data[1] = 0xFFu;
    pressure.data[2] = 0xFFu;
    pressure.data[3] = 0xFFu;
    if (PcbaProtocol_GetPressure001mmHg(&pressure, &pressure_001mmhg)) {
        fputs("invalid pressure sentinel was accepted\n", stderr);
        return 7;
    }

    ack.cmd = PCBA_CMD_ACK;
    ack.len = 1u;
    ack.data[0] = PCBA_ACK_YES;
    ack.crc_ok = 1u;
    if (!PcbaProtocol_IsSuccessAck(&ack)) {
        fputs("one-byte ACK YES was rejected\n", stderr);
        return 2;
    }
    ack.data[0] = PCBA_ACK_NO;
    if (PcbaProtocol_IsSuccessAck(&ack)) {
        fputs("ACK NO was accepted\n", stderr);
        return 3;
    }
    if (!PcbaProtocol_IsOneByteAck(&ack, 0u, PCBA_SINGLE_LOW_POWER_ACTIVE)) {
        fputs("single-PCBA low-power status 01 was rejected\n", stderr);
        return 8;
    }

    ack.len = 0u;
    ack.crc_ok = 0u;
    ack.raw_len = 8u;
    ack.raw[6] = 0x60u;
    ack.raw[7] = 0x0Au;
    if (!PcbaProtocol_IsSuccessAck(&ack)) {
        fputs("fixture legacy empty ACK was rejected\n", stderr);
        return 4;
    }
    ack.raw[6] ^= 0x01u;
    if (PcbaProtocol_IsSuccessAck(&ack)) {
        fputs("bad legacy empty ACK CRC was accepted\n", stderr);
        return 5;
    }

    puts("PCBA protocol smoke passed");
    return 0;
}
