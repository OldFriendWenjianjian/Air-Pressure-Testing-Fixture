#include "app_pressure_calibration.h"

#define APP_PRESSURE_CAL_MIN_RAW_SPAN        1000u
#define APP_PRESSURE_CAL_MIN_PRESSURE_SPAN   APP_PRESSURE_SCALE_PER_MMHG

int AppPressureCalibration_ValidateProfile(const AppPressureCalibrationProfile *profile)
{
    if (profile == 0 || profile->pressure_001mmhg[0] != 0u) {
        return -1;
    }
    for (uint8_t i = 0u; i < APP_PRESSURE_CAL_ANCHOR_COUNT; ++i) {
        if (profile->raw[i] > 0x00FFFFFFu ||
            profile->pressure_001mmhg[i] > APP_MPRLS_PRESSURE_MAX_001MMHG) {
            return -1;
        }
        if (i > 0u) {
            if (profile->raw[i] <= profile->raw[i - 1u] ||
                (profile->raw[i] - profile->raw[i - 1u]) < APP_PRESSURE_CAL_MIN_RAW_SPAN ||
                profile->pressure_001mmhg[i] <= profile->pressure_001mmhg[i - 1u] ||
                (profile->pressure_001mmhg[i] - profile->pressure_001mmhg[i - 1u]) <
                    APP_PRESSURE_CAL_MIN_PRESSURE_SPAN) {
                return -1;
            }
        }
    }
    return 0;
}

static uint32_t interpolate(uint32_t raw,
                            uint32_t raw0,
                            uint32_t raw1,
                            uint32_t pressure0,
                            uint32_t pressure1)
{
    const uint32_t raw_span = raw1 - raw0;
    const uint32_t pressure_span = pressure1 - pressure0;
    uint64_t scaled;

    if (raw_span == 0u) {
        return pressure0;
    }
    scaled = ((uint64_t)(raw - raw0) * pressure_span) + (raw_span / 2u);
    scaled = pressure0 + (scaled / raw_span);
    return scaled > APP_MPRLS_PRESSURE_MAX_001MMHG ?
           APP_MPRLS_PRESSURE_MAX_001MMHG : (uint32_t)scaled;
}

uint32_t AppPressureCalibration_ConvertProfile(const AppPressureCalibrationProfile *profile,
                                               uint32_t raw)
{
    uint8_t segment;

    if (AppPressureCalibration_ValidateProfile(profile) != 0 || raw <= profile->raw[0]) {
        return 0u;
    }
    for (segment = 0u; segment < (APP_PRESSURE_CAL_ANCHOR_COUNT - 1u); ++segment) {
        if (raw <= profile->raw[segment + 1u]) {
            return interpolate(raw,
                               profile->raw[segment],
                               profile->raw[segment + 1u],
                               profile->pressure_001mmhg[segment],
                               profile->pressure_001mmhg[segment + 1u]);
        }
    }

    segment = APP_PRESSURE_CAL_ANCHOR_COUNT - 2u;
    return interpolate(raw,
                       profile->raw[segment],
                       profile->raw[segment + 1u],
                       profile->pressure_001mmhg[segment],
                       profile->pressure_001mmhg[segment + 1u]);
}
