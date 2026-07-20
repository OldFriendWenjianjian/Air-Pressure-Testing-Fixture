#ifndef APP_PRESSURE_SCOPE_LOGIC_H
#define APP_PRESSURE_SCOPE_LOGIC_H

#include <stdint.h>

#define APP_PRESSURE_TANK_SENSOR_MASK    ((uint16_t)0x003Fu)
#define APP_PRESSURE_OUTPUT_SENSOR_MASK  ((uint16_t)0x3FC0u)
#define APP_PRESSURE_ALL_SENSOR_MASK     ((uint16_t)(APP_PRESSURE_TANK_SENSOR_MASK | \
                                                     APP_PRESSURE_OUTPUT_SENSOR_MASK))

static inline uint8_t AppPressureScope_HasBlockingFault(uint32_t fault_latched_mask,
                                                        uint16_t required_sensor_mask)
{
    return (fault_latched_mask & required_sensor_mask) != 0u ? 1u : 0u;
}

#endif
