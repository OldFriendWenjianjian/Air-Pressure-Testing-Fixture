#include "app_protocol.h"

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint16_t PcbaProtocol_Crc16Modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

size_t PcbaProtocol_Build(uint8_t cmd,
                          uint8_t channel,
                          const uint8_t *data,
                          uint16_t len,
                          uint8_t *out,
                          size_t out_size)
{
    size_t frame_len = 2u + 1u + 1u + 2u + len + 2u;

    if (out == 0 || frame_len > out_size || len > PCBA_FRAME_MAX_DATA) {
        return 0u;
    }
    if (len > 0u && data == 0) {
        return 0u;
    }

    out[0] = PCBA_FRAME_HEAD0;
    out[1] = PCBA_FRAME_HEAD1;
    out[2] = cmd;
    out[3] = channel;
    put_u16_le(&out[4], len);

    for (uint16_t i = 0; i < len; ++i) {
        out[6u + i] = data[i];
    }

    uint16_t crc = PcbaProtocol_Crc16Modbus(&out[2], (size_t)(1u + 1u + 2u + len));
    put_u16_le(&out[6u + len], crc);

    return frame_len;
}

size_t PcbaProtocol_BuildNoData(uint8_t cmd, uint8_t channel, uint8_t *out, size_t out_size)
{
    return PcbaProtocol_Build(cmd, channel, 0, 0u, out, out_size);
}

size_t PcbaProtocol_BuildPressure(uint8_t cmd,
                                  uint8_t channel,
                                  uint32_t pressure_001mmhg,
                                  uint8_t *out,
                                  size_t out_size)
{
    uint8_t data[4];

    put_u32_le(data, pressure_001mmhg);
    return PcbaProtocol_Build(cmd, channel, data, sizeof(data), out, out_size);
}

bool PcbaProtocol_Parse(const uint8_t *bytes, size_t len, PcbaFrame *frame)
{
    if (bytes == 0 || frame == 0 || len < 8u) {
        return false;
    }
    if (bytes[0] != PCBA_FRAME_HEAD0 || bytes[1] != PCBA_FRAME_HEAD1) {
        return false;
    }

    uint16_t data_len = (uint16_t)(bytes[4] | ((uint16_t)bytes[5] << 8));
    size_t expected = 2u + 1u + 1u + 2u + data_len + 2u;
    if (data_len > PCBA_FRAME_MAX_DATA || len < expected) {
        return false;
    }

    uint16_t rx_crc = (uint16_t)(bytes[6u + data_len] | ((uint16_t)bytes[7u + data_len] << 8));
    uint16_t calc_crc = PcbaProtocol_Crc16Modbus(&bytes[2], (size_t)(1u + 1u + 2u + data_len));
    if (rx_crc != calc_crc) {
        return false;
    }

    frame->cmd = bytes[2];
    frame->channel = bytes[3];
    frame->len = data_len;
    for (uint16_t i = 0; i < data_len; ++i) {
        frame->data[i] = bytes[6u + i];
    }

    return true;
}
