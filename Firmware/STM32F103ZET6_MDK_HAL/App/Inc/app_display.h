#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "app_state_machine.h"

void AppDisplay_Init(AppBootMode mode);
void AppDisplay_Task(void);
uint8_t AppDisplay_NeedsHardwareInit(void);
void AppDisplay_NotifyHardwareReady(void);

#endif
