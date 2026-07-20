#include <stdint.h>
#include <stdio.h>

#include "app_pcba_soft_uart_logic.h"

int main(void)
{
    AppPcbaSoftUartRxBuffer buffer = {0};
    const uint8_t pressure_frame[] = {
        0x55u, 0xAAu, 0x11u, 0x00u, 0x04u, 0x00u,
        0xA4u, 0x36u, 0x02u, 0x00u, 0x43u, 0x11u
    };
    uint8_t byte = 0u;

    if (AppPcbaSoftUart_ClampTimeoutMs(0u) != 0u ||
        AppPcbaSoftUart_ClampTimeoutMs(5u) != 5u ||
        AppPcbaSoftUart_ClampTimeoutMs(10u) != 10u ||
        AppPcbaSoftUart_ClampTimeoutMs(2000u) != 10u) {
        fputs("soft UART timeout was not capped at 10ms\n", stderr);
        return 1;
    }

    if (AppPcbaSoftUartRxBuffer_Load(&buffer,
                                      pressure_frame,
                                      sizeof(pressure_frame)) == 0u) {
        fputs("complete pressure burst was not buffered\n", stderr);
        return 2;
    }
    for (uint8_t i = 0u; i < sizeof(pressure_frame); ++i) {
        if (AppPcbaSoftUartRxBuffer_Pop(&buffer, &byte) == 0u ||
            byte != pressure_frame[i]) {
            fputs("byte-by-byte parser read lost buffered burst data\n", stderr);
            return 3;
        }
    }
    if (AppPcbaSoftUartRxBuffer_Pop(&buffer, &byte) != 0u ||
        buffer.count != 0u || buffer.read_index != 0u) {
        fputs("soft UART buffer did not finish empty\n", stderr);
        return 4;
    }
    if (AppPcbaSoftUartRxBuffer_Load(&buffer,
                                      pressure_frame,
                                      sizeof(pressure_frame)) == 0u ||
        AppPcbaSoftUartRxBuffer_Load(&buffer,
                                      pressure_frame,
                                      sizeof(pressure_frame)) != 0u) {
        fputs("soft UART buffer accepted overwrite of unread bytes\n", stderr);
        return 5;
    }

    puts("PCBA eight-channel soft UART buffering and 10ms timeout smoke passed");
    return 0;
}
