#ifndef APP_CURRENT_H
#define APP_CURRENT_H

#include <stdint.h>

typedef enum {
    APP_CURRENT_MODE_STANDBY = 0,
    APP_CURRENT_MODE_WORK
} AppCurrentMode;

void AppCurrent_Init(void);
int AppCurrent_CaptureAll(AppCurrentMode mode);
uint16_t AppCurrent_GetRaw(uint8_t channel);
uint32_t AppCurrent_GetStandbyUa(uint8_t channel);
uint32_t AppCurrent_GetWorkUa(uint8_t channel);
int AppCurrent_StandbyAllInRange(void);
int AppCurrent_WorkAllInRange(void);

#endif
