#include <stdint.h>
#include <stdio.h>

#include "app_pressure_scope_logic.h"

int main(void)
{
    if (APP_PRESSURE_TANK_SENSOR_MASK != 0x003Fu ||
        APP_PRESSURE_OUTPUT_SENSOR_MASK != 0x3FC0u ||
        APP_PRESSURE_ALL_SENSOR_MASK != 0x3FFFu) {
        fputs("pressure sensor masks are invalid\n", stderr);
        return 1;
    }
    if (AppPressureScope_HasBlockingFault(0x3FC0u,
                                          APP_PRESSURE_TANK_SENSOR_MASK) != 0u) {
        fputs("unplugged output sensors blocked tank closed-loop\n", stderr);
        return 2;
    }
    for (uint8_t sensor = 0u; sensor < 6u; ++sensor) {
        if (AppPressureScope_HasBlockingFault((uint32_t)1u << sensor,
                                              APP_PRESSURE_TANK_SENSOR_MASK) == 0u) {
            fputs("tank sensor fault did not block tank closed-loop\n", stderr);
            return 3;
        }
    }
    if (AppPressureScope_HasBlockingFault(0x0040u,
                                          APP_PRESSURE_ALL_SENSOR_MASK) == 0u) {
        fputs("output sensor fault did not block full pressure test\n", stderr);
        return 4;
    }

    puts("pressure scope smoke passed");
    return 0;
}
