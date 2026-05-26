#ifndef APP_KEYS_H
#define APP_KEYS_H

#include <stdint.h>

int AppKeys_Key1Pressed(void);
int AppKeys_Key2Pressed(void);
int AppKeys_PressSwitchActive(void);
int AppKeys_Key1HeldAtBoot(uint32_t hold_ms);

#endif
