#ifndef APP_PCBA_SOFT_UART_LOGIC_H
#define APP_PCBA_SOFT_UART_LOGIC_H

#include "app_protocol.h"
#include <stdint.h>

#define APP_PCBA_SOFT_UART_MAX_WAIT_MS 10u

typedef struct {
    uint8_t bytes[PCBA_FRAME_MAX_SIZE];
    uint8_t read_index;
    uint8_t count;
} AppPcbaSoftUartRxBuffer;

static inline uint32_t AppPcbaSoftUart_ClampTimeoutMs(uint32_t requested_ms)
{
    return requested_ms < APP_PCBA_SOFT_UART_MAX_WAIT_MS ?
           requested_ms : APP_PCBA_SOFT_UART_MAX_WAIT_MS;
}

static inline void AppPcbaSoftUartRxBuffer_Clear(AppPcbaSoftUartRxBuffer *buffer)
{
    if (buffer == 0) {
        return;
    }
    buffer->read_index = 0u;
    buffer->count = 0u;
}

static inline uint8_t AppPcbaSoftUartRxBuffer_Load(
    AppPcbaSoftUartRxBuffer *buffer,
    const uint8_t *bytes,
    uint8_t count)
{
    if (buffer == 0 || bytes == 0 || count == 0u ||
        count > PCBA_FRAME_MAX_SIZE || buffer->count != 0u) {
        return 0u;
    }

    for (uint8_t i = 0u; i < count; ++i) {
        buffer->bytes[i] = bytes[i];
    }
    buffer->read_index = 0u;
    buffer->count = count;
    return 1u;
}

static inline uint8_t AppPcbaSoftUartRxBuffer_Pop(
    AppPcbaSoftUartRxBuffer *buffer,
    uint8_t *byte)
{
    if (buffer == 0 || byte == 0 || buffer->count == 0u) {
        return 0u;
    }

    *byte = buffer->bytes[buffer->read_index++];
    --buffer->count;
    if (buffer->count == 0u) {
        buffer->read_index = 0u;
    }
    return 1u;
}

#endif
