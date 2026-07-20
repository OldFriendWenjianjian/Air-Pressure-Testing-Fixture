#include <stdint.h>
#include <stdio.h>

#include "app_pressure_math_saturation_logic.h"

int main(void)
{
    AppPressureMathSaturationRecovery recovery;

    AppPressureMathSaturation_Init(&recovery);
    if (AppPressureMathSaturation_RecordFault(&recovery, 100u, 500u, 3u) == 0u ||
        recovery.event_count != 1u || recovery.attempt_count != 1u) {
        fputs("first saturation event was not recorded\n", stderr);
        return 1;
    }
    if (AppPressureMathSaturation_RecordFault(&recovery, 599u, 500u, 3u) != 0u ||
        recovery.event_count != 1u || recovery.attempt_count != 1u) {
        fputs("retry interval was not enforced\n", stderr);
        return 2;
    }
    if (AppPressureMathSaturation_RecordFault(&recovery, 600u, 500u, 3u) == 0u ||
        AppPressureMathSaturation_RecordFault(&recovery, 1100u, 500u, 3u) == 0u ||
        AppPressureMathSaturation_RecordFault(&recovery, 1600u, 500u, 3u) != 0u ||
        recovery.attempt_count != 3u) {
        fputs("bounded recovery attempts failed\n", stderr);
        return 3;
    }
    if (AppPressureMathSaturation_RecordRecovery(&recovery) == 0u ||
        recovery.success_count != 1u || recovery.active != 0u ||
        AppPressureMathSaturation_RecordRecovery(&recovery) != 0u) {
        fputs("recovery success was not counted once\n", stderr);
        return 4;
    }
    if (AppPressureMathSaturation_RecordFault(&recovery, 2000u, 500u, 3u) == 0u ||
        recovery.event_count != 2u || recovery.attempt_count != 4u) {
        fputs("second saturation episode was not counted\n", stderr);
        return 5;
    }

    recovery.event_count = UINT16_MAX;
    recovery.attempt_count = UINT16_MAX;
    recovery.success_count = UINT16_MAX;
    recovery.active = 0u;
    (void)AppPressureMathSaturation_RecordFault(&recovery, 3000u, 500u, 3u);
    (void)AppPressureMathSaturation_RecordRecovery(&recovery);
    if (recovery.event_count != UINT16_MAX ||
        recovery.attempt_count != UINT16_MAX ||
        recovery.success_count != UINT16_MAX) {
        fputs("diagnostic counters did not saturate\n", stderr);
        return 6;
    }

    puts("pressure math saturation recovery smoke passed");
    return 0;
}
