#include "app_pcba_rx_stream.h"

static void clear_frame(PcbaFrame *frame)
{
    if (frame == 0) {
        return;
    }
    frame->cmd = 0u;
    frame->channel = 0u;
    frame->len = 0u;
    frame->raw_len = 0u;
    frame->crc_ok = 0u;
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_DATA; ++i) {
        frame->data[i] = 0u;
    }
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_SIZE; ++i) {
        frame->raw[i] = 0u;
    }
}

static void copy_raw_frame(const uint8_t *raw, uint8_t raw_len, PcbaFrame *frame)
{
    uint16_t declared_len = 0u;

    clear_frame(frame);
    if (raw == 0 || frame == 0 || raw_len == 0u) {
        return;
    }
    if (PcbaProtocol_Parse(raw, raw_len, frame)) {
        return;
    }

    frame->raw_len = raw_len <= PCBA_FRAME_MAX_SIZE ? raw_len : PCBA_FRAME_MAX_SIZE;
    for (uint8_t i = 0u; i < frame->raw_len; ++i) {
        frame->raw[i] = raw[i];
    }
    if (raw_len < 6u || raw[0] != PCBA_FRAME_HEAD0 || raw[1] != PCBA_FRAME_HEAD1) {
        frame->cmd = raw[0];
        frame->len = raw_len;
        for (uint8_t i = 0u; i < raw_len && i < PCBA_FRAME_MAX_DATA; ++i) {
            frame->data[i] = raw[i];
        }
        return;
    }

    frame->cmd = raw[2];
    frame->channel = raw[3];
    declared_len = (uint16_t)raw[4] | ((uint16_t)raw[5] << 8);
    frame->len = declared_len;
    for (uint16_t i = 0u;
         i < declared_len && i < PCBA_FRAME_MAX_DATA && (6u + i) < raw_len;
         ++i) {
        frame->data[i] = raw[6u + i];
    }
}

static void remember_diagnostic_byte(AppPcbaRxStream *stream, uint8_t byte)
{
    if (stream->diagnostic_len < PCBA_FRAME_MAX_SIZE) {
        stream->diagnostic_bytes[stream->diagnostic_len++] = byte;
        return;
    }
    for (uint8_t i = 1u; i < PCBA_FRAME_MAX_SIZE; ++i) {
        stream->diagnostic_bytes[i - 1u] = stream->diagnostic_bytes[i];
    }
    stream->diagnostic_bytes[PCBA_FRAME_MAX_SIZE - 1u] = byte;
}

void AppPcbaRxStream_Init(AppPcbaRxStream *stream)
{
    if (stream == 0) {
        return;
    }
    stream->frame_len = 0u;
    stream->expected_frame_len = 0u;
    stream->diagnostic_len = 0u;
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_SIZE; ++i) {
        stream->frame_bytes[i] = 0u;
        stream->diagnostic_bytes[i] = 0u;
    }
}

uint8_t AppPcbaRxStream_Push(AppPcbaRxStream *stream,
                             uint8_t byte,
                             PcbaFrame *candidate)
{
    uint16_t data_len;

    if (stream == 0 || candidate == 0) {
        return 0u;
    }
    remember_diagnostic_byte(stream, byte);

    if (stream->frame_len == 0u) {
        if (byte == PCBA_FRAME_HEAD0) {
            stream->frame_bytes[0] = byte;
            stream->frame_len = 1u;
        }
        return 0u;
    }
    if (stream->frame_len == 1u) {
        if (byte == PCBA_FRAME_HEAD1) {
            stream->frame_bytes[1] = byte;
            stream->frame_len = 2u;
        } else if (byte != PCBA_FRAME_HEAD0) {
            stream->frame_len = 0u;
        }
        return 0u;
    }
    if (stream->frame_len < 6u &&
        stream->frame_bytes[stream->frame_len - 1u] == PCBA_FRAME_HEAD0 &&
        byte == PCBA_FRAME_HEAD1) {
        stream->frame_bytes[0] = PCBA_FRAME_HEAD0;
        stream->frame_bytes[1] = PCBA_FRAME_HEAD1;
        stream->frame_len = 2u;
        stream->expected_frame_len = 0u;
        return 0u;
    }

    stream->frame_bytes[stream->frame_len++] = byte;
    if (stream->frame_len == 6u) {
        data_len = (uint16_t)stream->frame_bytes[4] |
                   ((uint16_t)stream->frame_bytes[5] << 8);
        if (data_len > PCBA_FRAME_MAX_DATA) {
            copy_raw_frame(stream->frame_bytes, stream->frame_len, candidate);
            if (byte == PCBA_FRAME_HEAD0) {
                stream->frame_bytes[0] = PCBA_FRAME_HEAD0;
                stream->frame_len = 1u;
            } else {
                stream->frame_len = 0u;
            }
            stream->expected_frame_len = 0u;
            return 1u;
        }
        stream->expected_frame_len = (uint8_t)(8u + data_len);
    }
    if (stream->expected_frame_len != 0u &&
        stream->frame_len >= stream->expected_frame_len) {
        copy_raw_frame(stream->frame_bytes, stream->frame_len, candidate);
        stream->frame_len = 0u;
        stream->expected_frame_len = 0u;
        return 1u;
    }
    return 0u;
}

void AppPcbaRxStream_CopyDiagnostic(const AppPcbaRxStream *stream,
                                    PcbaFrame *frame)
{
    if (stream == 0 || frame == 0) {
        return;
    }
    if (stream->frame_len > 0u) {
        copy_raw_frame(stream->frame_bytes, stream->frame_len, frame);
    } else {
        copy_raw_frame(stream->diagnostic_bytes, stream->diagnostic_len, frame);
    }
}

uint8_t AppPcbaRxFrame_IsExpected(const PcbaFrame *frame,
                                  uint8_t expected_cmd,
                                  uint16_t expected_len)
{
    if (frame == 0 || frame->cmd != expected_cmd) {
        return 0u;
    }
    if (expected_cmd == PCBA_CMD_ACK) {
        if (PcbaProtocol_IsSuccessAck(frame)) {
            return 1u;
        }
        return frame->crc_ok != 0u &&
               frame->len == 1u &&
               frame->data[0] == PCBA_ACK_NO ? 1u : 0u;
    }
    return frame->crc_ok != 0u && frame->len == expected_len ? 1u : 0u;
}
