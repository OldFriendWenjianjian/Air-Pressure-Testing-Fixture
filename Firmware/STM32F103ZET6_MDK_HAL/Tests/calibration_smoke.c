#include <stdint.h>
#include <stdio.h>

#include "app_pressure_calibration.h"
#include "app_pressure_calibration_store_logic.h"
#include "app_sensor_calibration.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        fprintf(stderr, "%s: got %lu expected %lu\n",
                name, (unsigned long)actual, (unsigned long)expected);
        return -1;
    }
    return 0;
}

static int test_math(void)
{
    AppPressureCalibrationProfile profile = {
        {100000u, 1100000u, 2100000u, 3100000u},
        {0u, 50000u, 150000u, 250000u}
    };

    if (AppPressureCalibration_ValidateProfile(&profile) != 0 ||
        expect_u32("below zero", AppPressureCalibration_ConvertProfile(&profile, 90000u), 0u) != 0 ||
        expect_u32("first midpoint", AppPressureCalibration_ConvertProfile(&profile, 600000u), 25000u) != 0 ||
        expect_u32("anchor two", AppPressureCalibration_ConvertProfile(&profile, 2100000u), 150000u) != 0 ||
        expect_u32("last extrapolation", AppPressureCalibration_ConvertProfile(&profile, 3350000u), 275000u) != 0 ||
        expect_u32("max clamp", AppPressureCalibration_ConvertProfile(&profile, 4000000u), 300000u) != 0) {
        return -1;
    }
    profile.raw[2] = profile.raw[1];
    return AppPressureCalibration_ValidateProfile(&profile) != 0 ? 0 : -1;
}

static int expect_route(const AppSensorCalibrationRoute *route,
                        uint16_t scan_mask,
                        uint8_t source,
                        uint8_t inlet,
                        uint8_t outlet,
                        uint8_t relief,
                        uint8_t channel)
{
    return route->scan_mask == scan_mask &&
           route->source_index == source &&
           route->inlet_valve == inlet &&
           route->outlet_valve == outlet &&
           route->relief_valve == relief &&
           route->channel_valve == channel ? 0 : -1;
}

static int test_routes(void)
{
    AppSensorCalibrationRoute route;

    if (AppSensorCalibration_ResolveRoute(0u, 13u, &route) != 0 ||
        expect_route(&route, 0x0001u, 0u, 1u, 2u, 21u, 0u) != 0 ||
        route.destination_slot != 13u || route.in_place_mode != 0u) {
        return -1;
    }
    if (AppSensorCalibration_ResolveRoute(1u, 5u, &route) != 0 ||
        expect_route(&route, 0x0020u, 5u, 11u, 12u, 26u, 0u) != 0) {
        return -1;
    }
    if (AppSensorCalibration_ResolveRoute(1u, 13u, &route) != 0 ||
        expect_route(&route, 0x2001u, 13u, 1u, 2u, 21u, 20u) != 0) {
        return -1;
    }
    return AppSensorCalibration_ResolveRoute(2u, 0u, &route) != 0 ? 0 : -1;
}

static int test_protocol_contract(void)
{
    AppSensorCalibrationRequest request;
    const uint8_t enter_ok[] = {1u, 1u, 14u};
    const uint8_t enter_short[] = {1u, 1u};
    const uint8_t enter_bad_mode[] = {1u, 2u, 1u};
    const uint8_t jog_bad_actuator[] = {3u, 3u, 0x2Cu, 0x01u};
    const uint8_t record_short[] = {6u, 2u, 0u, 0u, 0u};
    const uint8_t save_bad_slot[] = {7u, 15u};

    if (AppSensorCalibration_DecodeRequest(enter_ok, sizeof(enter_ok), &request) !=
            APP_SENSOR_CAL_REQUEST_OK ||
        request.mode != 1u || request.slot != 14u ||
        AppSensorCalibration_DecodeRequest(enter_short, sizeof(enter_short), &request) !=
            APP_SENSOR_CAL_REQUEST_BAD_LENGTH ||
        AppSensorCalibration_DecodeRequest(enter_bad_mode, sizeof(enter_bad_mode), &request) !=
            APP_SENSOR_CAL_REQUEST_BAD_VALUE ||
        AppSensorCalibration_DecodeRequest(jog_bad_actuator, sizeof(jog_bad_actuator), &request) !=
            APP_SENSOR_CAL_REQUEST_BAD_VALUE ||
        AppSensorCalibration_DecodeRequest(record_short, sizeof(record_short), &request) !=
            APP_SENSOR_CAL_REQUEST_BAD_LENGTH ||
        AppSensorCalibration_DecodeRequest(save_bad_slot, sizeof(save_bad_slot), &request) !=
            APP_SENSOR_CAL_REQUEST_BAD_VALUE) {
        return -1;
    }
    return 0;
}

static int test_store_recovery_policy(void)
{
    if (AppPressureCalibration_SelectInactivePage(APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS) !=
            APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS ||
        AppPressureCalibration_SelectInactivePage(APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS) !=
            APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS ||
        AppPressureCalibration_ClearNeedsPersist(0u, 0u, 1u) == 0u ||
        AppPressureCalibration_ClearNeedsPersist((uint16_t)(1u << 13), 13u, 0u) == 0u ||
        AppPressureCalibration_ClearNeedsPersist(0u, 13u, 0u) != 0u ||
        AppPressureCalibration_ClearNeedsPersist(0u, 14u, 1u) != 0u) {
        return -1;
    }
    return 0;
}

int main(void)
{
    if (test_math() != 0 || test_routes() != 0 || test_protocol_contract() != 0 ||
        test_store_recovery_policy() != 0) {
        fprintf(stderr, "calibration smoke failed\n");
        return 1;
    }
    puts("calibration math, route, protocol, and store recovery smoke passed");
    return 0;
}
