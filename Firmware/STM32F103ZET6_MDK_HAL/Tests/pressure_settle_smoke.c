#include <stdint.h>
#include <stdio.h>

#include "app_pressure_settle_logic.h"

int main(void)
{
    if (AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_SENSOR_FAULT) != 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_SENSOR_INVALID) != 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_OVERPRESSURE) != 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_NO_PRESSURE_RISE) == 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_TIMEOUT) == 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_TREND_SAMPLES) == 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_TREND_RESIDUAL) == 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_TREND_DROP_RATE) == 0u ||
        AppPressureSettle_FailureCanContinue(APP_PRESSURE_SETTLE_FAILURE_TREND_DIRECTION) == 0u) {
        fputs("pressure failure continuation classification failed\n", stderr);
        return 8;
    }
    if (AppPressureSettle_SelectApproach(250000u, 100000u, 5000u) !=
            APP_PRESSURE_SETTLE_APPROACH_RELIEF ||
        AppPressureSettle_SelectApproach(105000u, 100000u, 5000u) !=
            APP_PRESSURE_SETTLE_APPROACH_SETTLE ||
        AppPressureSettle_SelectApproach(99000u, 100000u, 5000u) !=
            APP_PRESSURE_SETTLE_APPROACH_FILL ||
        AppPressureSettle_SelectApproach(104000u, 100000u, 5000u) !=
            APP_PRESSURE_SETTLE_APPROACH_SETTLE ||
        AppPressureSettle_SelectApproach(106000u, 100000u, 5000u) !=
            APP_PRESSURE_SETTLE_APPROACH_RELIEF) {
        fputs("fill/relief/settle approach selection failed\n", stderr);
        return 7;
    }
    if (AppPressureSettle_WindowReady(155000u, 153000u, 150000u, 5000u,
                                      2999u, 3000u, 1000u) != 0u) {
        fputs("settle window completed before wait time\n", stderr);
        return 1;
    }
    if (AppPressureSettle_DropRate001mmHgPerSecond(155000u, 153000u, 3000u) != 666u ||
        AppPressureSettle_WindowReady(155000u, 153000u, 150000u, 5000u,
                                      3000u, 3000u, 1000u) == 0u) {
        fputs("acceptable pressure decline did not pass\n", stderr);
        return 2;
    }
    if (AppPressureSettle_WindowReady(155000u, 151000u, 150000u, 5000u,
                                      3000u, 3000u, 1000u) != 0u) {
        fputs("excessive pressure decline passed\n", stderr);
        return 3;
    }
    if (AppPressureSettle_WindowReady(155000u, 151999u, 150000u, 5000u,
                                      3000u, 3000u, 1000u) != 0u) {
        fputs("fractionally excessive pressure decline passed after rounding\n", stderr);
        return 6;
    }
    if (AppPressureSettle_WindowReady(155000u, 149000u, 150000u, 5000u,
                                      3000u, 3000u, 5000u) != 0u) {
        fputs("pressure below target passed\n", stderr);
        return 4;
    }
    if (AppPressureSettle_WindowReady(155000u, 156000u, 150000u, 5000u,
                                      3000u, 3000u, 5000u) != 0u) {
        fputs("pressure above overshoot tolerance passed\n", stderr);
        return 9;
    }
    if (AppPressureSettle_DropRate001mmHgPerSecond(155000u, 156000u, 3000u) != 0u ||
        AppPressureSettle_DropRate001mmHgPerSecond(155000u, 150000u, 0u) != 0u) {
        fputs("rising pressure or zero-time handling failed\n", stderr);
        return 5;
    }
    puts("pressure settle smoke passed");
    return 0;
}
