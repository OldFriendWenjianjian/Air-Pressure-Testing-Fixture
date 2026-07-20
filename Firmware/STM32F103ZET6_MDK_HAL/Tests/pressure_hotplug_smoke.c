#include <stdint.h>
#include <stdio.h>

#include "app_pressure_hotplug_logic.h"

int main(void)
{
    AppPressureHotplugRecovery recovery = {0u};

    for (uint8_t sample = 0u; sample < 3u; ++sample) {
        (void)AppPressureHotplug_RecordValid(&recovery, 5u);
    }
    AppPressureHotplug_Reset(&recovery);

    for (uint8_t sample = 1u; sample < 5u; ++sample) {
        if (AppPressureHotplug_RecordValid(&recovery, 5u) != 0u) {
            fprintf(stderr, "recovered too early at sample %u\n", sample);
            return 1;
        }
    }
    if (AppPressureHotplug_RecordValid(&recovery, 5u) == 0u) {
        fputs("did not recover on fifth valid sample\n", stderr);
        return 2;
    }
    if (AppPressureHotplug_RecordValid(&recovery, 5u) == 0u) {
        fputs("recovery did not remain saturated\n", stderr);
        return 3;
    }

    AppPressureHotplug_Reset(&recovery);
    if (recovery.consecutive_valid_samples != 0u ||
        AppPressureHotplug_RecordValid(&recovery, 0u) != 0u ||
        AppPressureHotplug_RecordValid(0, 5u) != 0u) {
        fputs("reset or invalid argument handling failed\n", stderr);
        return 4;
    }

    puts("pressure hotplug recovery smoke passed");
    return 0;
}
