#ifndef APP_PRESSURE_H
#define APP_PRESSURE_H

#include <stdint.h>

typedef enum {
    PRESSURE_SENSOR_TANK_50 = 0,
    PRESSURE_SENSOR_TANK_150,
    PRESSURE_SENSOR_TANK_250,
    PRESSURE_SENSOR_TANK_100,
    PRESSURE_SENSOR_TANK_200,
    PRESSURE_SENSOR_TANK_285,
    PRESSURE_SENSOR_CH1,
    PRESSURE_SENSOR_CH2,
    PRESSURE_SENSOR_CH3,
    PRESSURE_SENSOR_CH4,
    PRESSURE_SENSOR_CH5,
    PRESSURE_SENSOR_CH6,
    PRESSURE_SENSOR_CH7,
    PRESSURE_SENSOR_CH8
} PressureSensorIndex;

typedef enum {
    APP_PRESSURE_FAULT_NONE = 0,
    APP_PRESSURE_FAULT_MEASURE_CMD_FAILED,
    APP_PRESSURE_FAULT_READ_FAILED,
    APP_PRESSURE_FAULT_NOT_POWERED,
    APP_PRESSURE_FAULT_BUSY_TIMEOUT,
    APP_PRESSURE_FAULT_MEMORY_INTEGRITY,
    APP_PRESSURE_FAULT_MATH_SATURATION,
    APP_PRESSURE_FAULT_STATUS_INVALID
} AppPressureFaultCode;

void AppPressure_Init(void);
void AppPressure_Task(void);
uint32_t AppPressure_GetRaw(PressureSensorIndex index);
uint32_t AppPressure_Get001mmHg(PressureSensorIndex index);
uint32_t AppPressure_GetNominal001mmHg(PressureSensorIndex index);
uint32_t AppPressure_GetSafety001mmHg(PressureSensorIndex index);
uint32_t AppPressure_GetSampleSequence(PressureSensorIndex index);
void AppPressure_SetScanMask(uint16_t sensor_mask);
int AppPressure_IsValid(PressureSensorIndex index);
int AppPressure_IsFaultLatched(PressureSensorIndex index);
uint32_t AppPressure_GetFaultLatchedMask(void);
int AppPressure_HasAnyFaultLatched(void);
uint8_t AppPressure_GetStatusByte(PressureSensorIndex index);
uint8_t AppPressure_GetFaultCode(PressureSensorIndex index);
uint16_t AppPressure_GetMathSaturationEventCount(PressureSensorIndex index);
uint16_t AppPressure_GetMathSaturationAttemptCount(PressureSensorIndex index);
uint16_t AppPressure_GetMathSaturationSuccessCount(PressureSensorIndex index);
int AppPressure_IsStable(PressureSensorIndex index, uint32_t target_001mmhg);
int AppPressure_AllChannelOutputsNear(uint32_t target_001mmhg);

#endif
