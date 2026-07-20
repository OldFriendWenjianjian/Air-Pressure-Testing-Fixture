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
    APP_STATE_PCBA_CURRENT_TEST,
    APP_STATE_RTC_DEBUG,
    APP_STATE_PCBA_TIMING_DIAGNOSTIC,
    APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC,
    APP_STATE_SINGLE_TANK_LOOP,
    APP_STATE_SINGLE_PCBA_FLOW,
    APP_STATE_PCBA_PRESSURE_QUERY,
    APP_STATE_PCBA_WRITE_FLASH,
    APP_STATE_SENSOR_CALIBRATION,
    APP_STATE_COUNT
} AppRuntimeState;

typedef enum {
    APP_SINGLE_TANK_PROTECTION_NONE = 0,
    APP_SINGLE_TANK_PROTECTION_SENSOR_FAULT = 1,
    APP_SINGLE_TANK_PROTECTION_NO_PRESSURE_RISE = 2
} AppSingleTankProtectionReason;

#define APP_PCBA_TIMING_STEP_COUNT 10u
#define APP_SINGLE_TANK_PCBA_STEP_COUNT 23u
#define APP_PCBA_REPORT_RAW_MAX 24u

typedef struct {
    uint8_t kind;
    uint8_t cmd_sent;
    uint8_t ok;
    uint8_t resp_cmd_or_byte;
    uint8_t resp_channel;
    uint8_t resp_len;
    uint8_t resp_data0;
    uint8_t resp_data1;
    uint8_t resp_data2;
    uint8_t resp_data3;
    uint8_t raw_len;
    uint8_t raw[APP_PCBA_REPORT_RAW_MAX];
    uint32_t elapsed_us;
} AppPcbaTimingEntry;

typedef struct {
    uint8_t running;
    uint8_t done;
    uint8_t count;
    uint8_t final_result;
    AppPcbaTimingEntry entries[APP_PCBA_TIMING_STEP_COUNT];
} AppPcbaTimingReport;

typedef struct {
    uint8_t kind;
    uint8_t cmd_sent;
    uint8_t ok;
    uint8_t flags;
    uint8_t resp_cmd_or_byte;
    uint8_t resp_channel;
    uint8_t resp_len;
    uint8_t resp_data0;
    uint8_t resp_data1;
    uint8_t resp_data2;
    uint8_t resp_data3;
    uint8_t raw_len;
    uint8_t raw[APP_PCBA_REPORT_RAW_MAX];
    uint32_t current_ua_x100;
    uint32_t elapsed_us;
    uint32_t parsed_value;
} AppSingleTankPcbaEntry;

typedef struct {
    uint8_t running;
    uint8_t done;
    uint8_t count;
    uint8_t final_result;
    uint32_t standby_current_ua_x100;
    uint32_t work_current_ua_x100;
    AppSingleTankPcbaEntry entries[APP_SINGLE_TANK_PCBA_STEP_COUNT];
} AppSingleTankPcbaReport;

void AppStateMachine_Init(AppBootMode mode);
void AppStateMachine_Task(void);
int AppStateMachine_RequestStart(void);
int AppStateMachine_RequestStop(void);
int AppStateMachine_RequestPause(void);
int AppStateMachine_RequestResume(void);
int AppStateMachine_RequestState(AppRuntimeState state);
AppRuntimeState AppStateMachine_GetState(void);
const char *AppStateMachine_GetStateName(AppRuntimeState state);
uint32_t AppStateMachine_GetStateElapsedMs(void);
uint8_t AppStateMachine_IsRunning(void);
uint8_t AppStateMachine_IsPaused(void);
uint8_t AppStateMachine_IsManualMode(void);
uint8_t AppStateMachine_IsError(void);
void AppStateMachine_SetManualMode(uint8_t enabled);
void AppStateMachine_SetManualValve(uint8_t valve_number, uint8_t open);
void AppStateMachine_SetManualValveMask(uint32_t valve_mask, uint32_t open_mask);
void AppStateMachine_SetPressureTolerance001mmHg(uint32_t tolerance_001mmhg);
uint32_t AppStateMachine_GetPressureTolerance001mmHg(void);
int AppStateMachine_RequestSingleTankLoop(uint8_t tank_index,
                                          uint32_t target_001mmhg,
                                          uint32_t tolerance_001mmhg);
int AppStateMachine_StopSingleTankLoop(void);
uint8_t AppStateMachine_IsSingleTankLoopActive(void);
uint8_t AppStateMachine_GetSingleTankIndex(void);
uint32_t AppStateMachine_GetSingleTankTarget001mmHg(void);
uint32_t AppStateMachine_GetSingleTankTolerance001mmHg(void);
uint8_t AppStateMachine_IsSingleTankProtectionActive(void);
uint8_t AppStateMachine_GetSingleTankProtectionReason(void);
uint8_t AppStateMachine_GetSingleTankProtectionTankIndex(void);
uint8_t AppStateMachine_GetSingleTankProtectionSensorIndex(void);
uint8_t AppStateMachine_GetSingleTankProtectionInletValve(void);
uint8_t AppStateMachine_IsSinglePcbaFlowActive(void);
void AppStateMachine_SetPcbaCurrent50mAEnabled(uint8_t enabled);
uint8_t AppStateMachine_IsPcbaCurrent50mAEnabled(void);
void AppStateMachine_SetPcbaSupply5VEnabled(uint8_t enabled);
uint8_t AppStateMachine_IsPcbaSupply5VEnabled(void);
uint8_t AppStateMachine_IsPcbaOnline(uint8_t channel);
uint8_t AppStateMachine_IsPcbaLowPowerOk(uint8_t channel);
uint8_t AppStateMachine_IsPcbaNormalPowerOk(uint8_t channel);
uint32_t AppStateMachine_GetPcbaTestPressure001mmHg(uint8_t channel);
int AppStateMachine_RequestPcbaTimingDiagnostic(uint8_t stop_on_fail);
void AppStateMachine_GetPcbaTimingReport(AppPcbaTimingReport *report);
int AppStateMachine_RequestSingleTankPcbaDiagnostic(
    uint8_t continue_on_fail,
    uint32_t trend_max_residual_001mmhg,
    uint32_t trend_window_ms,
    uint32_t max_drop_rate_001mmhg_per_s);
void AppStateMachine_GetSingleTankPcbaReport(AppSingleTankPcbaReport *report);
uint32_t AppStateMachine_GetPcbaTimingPassCount(void);
uint32_t AppStateMachine_GetPcbaTimingFailCount(void);
uint8_t AppStateMachine_IsPcbaProbeServiceDue(void);
int AppStateMachine_RequestSensorCalibrationEnter(uint8_t in_place_mode,
                                                  uint8_t destination_slot);
int AppStateMachine_RequestSensorCalibrationExit(void);
int AppStateMachine_SensorCalibrationJog(uint8_t actuator, uint16_t lease_ms);
int AppStateMachine_SensorCalibrationStartAutoVent(void);
int AppStateMachine_SensorCalibrationCancelAutoVent(void);
int AppStateMachine_SensorCalibrationRecord(uint8_t point_index, uint32_t actual_001mmhg);
int AppStateMachine_SensorCalibrationSaveSlot(uint8_t slot);
int AppStateMachine_SensorCalibrationClearSlot(uint8_t slot);
int AppStateMachine_SensorCalibrationResetSession(void);

#endif
