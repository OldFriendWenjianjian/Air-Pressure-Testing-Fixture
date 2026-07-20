#include <stdio.h>

#include "app_pressure_vent_logic.h"

static const AppPressureVentLimits k_limits = {
    100u,
    1000u,
    30000u,
    30000u,
    1000u,
    5u
};

static int test_zero_hold_and_static_confirmation(void)
{
    AppPressureVentController controller;
    AppPressureVentAction action;

    AppPressureVent_Init(&controller, 0u, 10u);
    action = AppPressureVent_Step(&controller, &k_limits, 0u, 10u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 100u, 100u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 1;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 29999u, 11u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 100u, 100u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 2;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 30000u, 12u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 100u, 100u);
    if (action != APP_PRESSURE_VENT_RELIEF_CLOSED) {
        return 3;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 30999u, 13u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 500u, 500u);
    if (action != APP_PRESSURE_VENT_RELIEF_CLOSED) {
        return 4;
    }
    for (uint32_t sample = 0u; sample < 5u; ++sample) {
        action = AppPressureVent_Step(&controller,
                                      &k_limits,
                                      31000u + (sample * 100u),
                                      14u + sample,
                                      1u,
                                      1u,
                                      APP_PRESSURE_FAULT_NONE,
                                      50u,
                                      50u);
    }
    return action == APP_PRESSURE_VENT_ZERO_CONFIRMED ? 0 : 5;
}

static int test_late_zero_and_rebound(void)
{
    AppPressureVentController controller;
    AppPressureVentAction action;

    AppPressureVent_Init(&controller, 0u, 1u);
    action = AppPressureVent_Step(&controller, &k_limits, 10000u, 2u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 5000u, 5000u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 1;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 11000u, 3u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 50u, 50u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 2;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 40999u, 4u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 50u, 50u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 3;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 41000u, 5u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 50u, 50u);
    if (action != APP_PRESSURE_VENT_RELIEF_CLOSED) {
        return 4;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 41999u, 6u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 10000u, 10000u);
    if (action != APP_PRESSURE_VENT_RELIEF_CLOSED) {
        return 5;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 42000u, 7u, 1u, 1u,
                                  APP_PRESSURE_FAULT_NONE, 10000u, 10000u);
    return action == APP_PRESSURE_VENT_RELIEF_OPEN ? 0 : 6;
}

static int test_math_saturation_during_open_vent(void)
{
    AppPressureVentController controller;
    AppPressureVentAction action;

    AppPressureVent_Init(&controller, 0u, 1u);
    action = AppPressureVent_Step(&controller, &k_limits, 0u, 1u, 0u, 0u,
                                  APP_PRESSURE_FAULT_MATH_SATURATION,
                                  0u, 0u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 1;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 1000u, 1u, 0u, 1u,
                                  APP_PRESSURE_FAULT_MATH_SATURATION,
                                  0u, 500u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 2;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 30999u, 1u, 0u, 1u,
                                  APP_PRESSURE_FAULT_MATH_SATURATION,
                                  0u, 500u);
    if (action != APP_PRESSURE_VENT_RELIEF_OPEN) {
        return 3;
    }
    action = AppPressureVent_Step(&controller, &k_limits, 31000u, 1u, 0u, 1u,
                                  APP_PRESSURE_FAULT_MATH_SATURATION,
                                  0u, 500u);
    return action == APP_PRESSURE_VENT_RELIEF_CLOSED ? 0 : 4;
}

int main(void)
{
    if (test_zero_hold_and_static_confirmation() != 0) {
        fputs("30 second zero hold or static confirmation failed\n", stderr);
        return 1;
    }
    if (test_late_zero_and_rebound() != 0) {
        fputs("late zero hold or rebound dwell failed\n", stderr);
        return 2;
    }
    if (test_math_saturation_during_open_vent() != 0) {
        fputs("math saturation ended the required open vent early\n", stderr);
        return 3;
    }

    puts("pressure vent 30 second hold and static settle smoke passed");
    return 0;
}
