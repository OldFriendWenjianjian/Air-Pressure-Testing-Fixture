#ifndef APP_PCBA_UART_H
#define APP_PCBA_UART_H

#include "app_protocol.h"
#include <stdint.h>

void AppPcbaUart_Init(void);
int AppPcbaUart_Send(uint8_t channel, const uint8_t *data, uint16_t len);
int AppPcbaUart_SendCommandAll(uint8_t cmd);
int AppPcbaUart_SendPressureAll(uint8_t cmd, uint32_t pressure_001mmhg);
int AppPcbaUart_RequestAll(uint8_t cmd, PcbaFrame *responses, uint32_t timeout_ms);

#endif
