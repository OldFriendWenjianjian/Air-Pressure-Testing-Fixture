#ifndef APP_PCBA_UART_H
#define APP_PCBA_UART_H

#include "app_protocol.h"
#include <stdint.h>

void AppPcbaUart_Init(void);
int AppPcbaUart_Send(uint8_t channel, const uint8_t *data, uint16_t len);
void AppPcbaUart_FlushRx(uint8_t channel);
int AppPcbaUart_WakeOne(uint8_t channel, uint8_t expected, uint32_t timeout_ms);
int AppPcbaUart_WakeAll(uint8_t expected, uint32_t timeout_ms);
int AppPcbaUart_WakeOneTimed(uint8_t channel,
                             uint8_t expected,
                             uint32_t timeout_ms,
                             uint32_t *elapsed_us,
                             uint8_t *response_byte);
int AppPcbaUart_SendCommandRoute(uint8_t route_channel,
                                 uint8_t frame_channel,
                                 uint8_t cmd,
                                 PcbaFrame *response,
                                 uint32_t timeout_ms);
int AppPcbaUart_SendCommandOne(uint8_t channel, uint8_t cmd, PcbaFrame *response, uint32_t timeout_ms);
int AppPcbaUart_SendCommandAll(uint8_t cmd, PcbaFrame *responses, uint32_t timeout_ms);
int AppPcbaUart_SendPressureRoute(uint8_t route_channel,
                                  uint8_t frame_channel,
                                  uint8_t cmd,
                                  uint32_t pressure_001mmhg,
                                  PcbaFrame *response,
                                  uint32_t timeout_ms);
int AppPcbaUart_SendPressureOne(uint8_t channel,
                                uint8_t cmd,
                                uint32_t pressure_001mmhg,
                                PcbaFrame *response,
                                uint32_t timeout_ms);
int AppPcbaUart_SendPressureAll(uint8_t cmd, uint32_t pressure_001mmhg, PcbaFrame *responses, uint32_t timeout_ms);
int AppPcbaUart_SendTestRoute(uint8_t route_channel,
                              uint8_t frame_channel,
                              uint32_t *pcba_pressure_001mmhg,
                              uint32_t timeout_ms);
int AppPcbaUart_SendTestOne(uint8_t channel, uint32_t *pcba_pressure_001mmhg, uint32_t timeout_ms);
int AppPcbaUart_SendTestAll(uint32_t *pcba_pressure_001mmhg, uint32_t timeout_ms);
int AppPcbaUart_RequestRoute(uint8_t route_channel,
                             uint8_t frame_channel,
                             uint8_t cmd,
                             PcbaFrame *response,
                             uint32_t timeout_ms);
int AppPcbaUart_RequestRouteTimed(uint8_t route_channel,
                                  uint8_t frame_channel,
                                  uint8_t cmd,
                                  PcbaFrame *response,
                                  uint32_t timeout_ms,
                                  uint32_t *elapsed_us);
int AppPcbaUart_RequestOne(uint8_t channel, uint8_t cmd, PcbaFrame *response, uint32_t timeout_ms);
int AppPcbaUart_RequestAll(uint8_t cmd, PcbaFrame *responses, uint32_t timeout_ms);
int AppPcbaUart_SendPressureRouteTimed(uint8_t route_channel,
                                       uint8_t frame_channel,
                                       uint8_t cmd,
                                       uint32_t pressure_001mmhg,
                                       PcbaFrame *response,
                                       uint32_t timeout_ms,
                                       uint32_t *elapsed_us);
int AppPcbaUart_SendDataRouteTimed(uint8_t route_channel,
                                   uint8_t frame_channel,
                                   uint8_t cmd,
                                   const uint8_t *data,
                                   uint16_t data_len,
                                   PcbaFrame *response,
                                   uint32_t timeout_ms,
                                   uint32_t *elapsed_us);
int AppPcbaUart_SendRawTimed(uint8_t route_channel,
                             const uint8_t *tx,
                             uint16_t tx_len,
                             PcbaFrame *response,
                             uint32_t timeout_ms,
                             uint32_t *elapsed_us);
int AppPcbaUart_SendTestRouteTimed(uint8_t route_channel,
                                   uint8_t frame_channel,
                                   uint32_t *pcba_pressure_001mmhg,
                                   uint32_t timeout_ms,
                                   uint32_t *elapsed_us);
int AppPcbaUart_CheckEmptyAckAll(const PcbaFrame *responses);
int AppPcbaUart_CheckOneByteAckAll(const PcbaFrame *responses, uint8_t expected);

#endif
