#ifndef APP_PCBA_RX_STREAM_H
#define APP_PCBA_RX_STREAM_H

#include "app_protocol.h"
#include <stdint.h>

typedef struct {
    uint8_t frame_bytes[PCBA_FRAME_MAX_SIZE];
    uint8_t frame_len;
    uint8_t expected_frame_len;
    uint8_t diagnostic_bytes[PCBA_FRAME_MAX_SIZE];
    uint8_t diagnostic_len;
} AppPcbaRxStream;

void AppPcbaRxStream_Init(AppPcbaRxStream *stream);
uint8_t AppPcbaRxStream_Push(AppPcbaRxStream *stream,
                             uint8_t byte,
                             PcbaFrame *candidate);
void AppPcbaRxStream_CopyDiagnostic(const AppPcbaRxStream *stream,
                                    PcbaFrame *frame);
uint8_t AppPcbaRxFrame_IsExpected(const PcbaFrame *frame,
                                  uint8_t expected_cmd,
                                  uint16_t expected_len);

#endif
