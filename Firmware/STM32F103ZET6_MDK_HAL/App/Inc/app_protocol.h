#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCBA_FRAME_HEAD0                    0x55u
#define PCBA_FRAME_HEAD1                    0xAAu
#define PCBA_FRAME_MAX_DATA                 16u
#define PCBA_FRAME_MAX_SIZE                 (2u + 1u + 1u + 2u + PCBA_FRAME_MAX_DATA + 2u)

typedef enum {
    PCBA_CMD_WAKE_UP                = 0x00u,
    PCBA_CMD_SET_TEST_MODE          = 0x01u,
    PCBA_CMD_RECORD_ZERO_AD         = 0x02u,
    PCBA_CMD_POWER_ON               = 0x03u,
    PCBA_CMD_QUERY_LOW_POWER_STATE  = 0x04u,
    PCBA_CMD_QUERY_NORMAL_POWER     = 0x05u,
    PCBA_CMD_SYNC_PRESSURE_CAL      = 0x10u,
    PCBA_CMD_PRESSURE_TEST          = 0x11u,
    PCBA_CMD_ACK                    = 0x7Fu
} PcbaCommand;

typedef enum {
    PCBA_ACK_YES                    = 0x00u,
    PCBA_ACK_NO                     = 0x01u
} PcbaAckData;

typedef struct {
    uint8_t cmd;
    uint8_t channel;
    uint16_t len;
    uint8_t data[PCBA_FRAME_MAX_DATA];
} PcbaFrame;

uint16_t PcbaProtocol_Crc16Modbus(const uint8_t *data, size_t len);
size_t PcbaProtocol_Build(uint8_t cmd,
                          uint8_t channel,
                          const uint8_t *data,
                          uint16_t len,
                          uint8_t *out,
                          size_t out_size);
size_t PcbaProtocol_BuildNoData(uint8_t cmd, uint8_t channel, uint8_t *out, size_t out_size);
size_t PcbaProtocol_BuildPressure(uint8_t cmd,
                                  uint8_t channel,
                                  uint32_t pressure_001mmhg,
                                  uint8_t *out,
                                  size_t out_size);
bool PcbaProtocol_IsEmptyAck(const PcbaFrame *frame, uint8_t channel);
bool PcbaProtocol_IsOneByteAck(const PcbaFrame *frame, uint8_t channel, uint8_t expected);
bool PcbaProtocol_GetU32Le(const PcbaFrame *frame, uint32_t *value);
bool PcbaProtocol_Parse(const uint8_t *bytes, size_t len, PcbaFrame *frame);

#endif
