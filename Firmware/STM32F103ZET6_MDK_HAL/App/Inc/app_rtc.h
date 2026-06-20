#ifndef APP_RTC_H
#define APP_RTC_H

#include <stdint.h>

#define APP_RTC_FLAG_INITIALIZED       0x01u
#define APP_RTC_FLAG_OSCILLATOR_READY  0x02u
#define APP_RTC_FLAG_BACKUP_VALID      0x04u

void AppRtc_Init(void);
uint32_t AppRtc_GetEpochSeconds(void);
uint8_t AppRtc_GetFlags(void);
int AppRtc_SetEpochSeconds(uint32_t epoch_seconds);

#endif
