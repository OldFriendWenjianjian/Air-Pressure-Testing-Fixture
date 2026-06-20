#ifndef APP_JLINK_RTT_CONTROL_H
#define APP_JLINK_RTT_CONTROL_H

#include <stdint.h>

int AppJlinkRttControl_Start(void);
int AppJlinkRttControl_Read(uint8_t *data, uint16_t max_len);
int AppJlinkRttControl_Write(const uint8_t *data, uint16_t len);
void AppJlinkRttControl_RequestMscReboot(void);
uint8_t AppJlinkRttControl_IsMscRebootPending(void);

#endif
