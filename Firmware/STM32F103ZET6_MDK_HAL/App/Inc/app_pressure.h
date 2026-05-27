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

void AppPressure_Init(void);
void AppPressure_Task(void);
uint32_t AppPressure_GetRaw(PressureSensorIndex index);
uint32_t AppPressure_Get001mmHg(PressureSensorIndex index);
int AppPressure_IsValid(PressureSensorIndex index);
int AppPressure_IsStable(PressureSensorIndex index, uint32_t target_001mmhg);
int AppPressure_AllChannelOutputsNear(uint32_t target_001mmhg);

#endif
