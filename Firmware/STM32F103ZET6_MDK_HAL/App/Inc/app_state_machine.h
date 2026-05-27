#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include <stdint.h>

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_USB_MSC
} AppBootMode;

typedef enum {
    APP_STATE_USB_MSC = 0,
    APP_STATE_INIT_TANKS,
    APP_STATE_AUTO_AIRTIGHTNESS,
    APP_STATE_READY,
    APP_STATE_PCBA_POWER_ON,
    APP_STATE_PCBA_STANDBY_CURRENT_CHECK,
    APP_STATE_PCBA_WAKE,
    APP_STATE_PCBA_WORK_CURRENT_MEASURE,
    APP_STATE_PCBA_SET_TEST_MODE,
    APP_STATE_PCBA_ZERO,
    APP_STATE_SWITCH_45V,
    APP_STATE_LOW_POWER_QUERY,
    APP_STATE_SWITCH_5V,
    APP_STATE_NORMAL_POWER_QUERY,
    APP_STATE_CAL_50,
    APP_STATE_CAL_150,
    APP_STATE_CAL_250,
    APP_STATE_TEST_100,
    APP_STATE_TEST_200,
    APP_STATE_TEST_285,
    APP_STATE_RESULT,
    APP_STATE_REFILL,
    APP_STATE_ERROR,
    APP_STATE_COUNT
} AppRuntimeState;

void AppStateMachine_Init(AppBootMode mode);
void AppStateMachine_Task(void);
AppRuntimeState AppStateMachine_GetState(void);
const char *AppStateMachine_GetStateName(AppRuntimeState state);
uint32_t AppStateMachine_GetStateElapsedMs(void);
uint8_t AppStateMachine_IsPcbaOnline(uint8_t channel);
uint8_t AppStateMachine_IsPcbaLowPowerOk(uint8_t channel);
uint8_t AppStateMachine_IsPcbaNormalPowerOk(uint8_t channel);
uint32_t AppStateMachine_GetPcbaTestPressure001mmHg(uint8_t channel);

#endif
