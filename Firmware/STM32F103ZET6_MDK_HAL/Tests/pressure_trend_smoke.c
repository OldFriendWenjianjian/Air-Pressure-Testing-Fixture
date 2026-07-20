#include <stdint.h>
#include <stdio.h>

#include "app_pressure_trend_logic.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: got %lu, expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

int main(void)
{
    AppPressureTrendSamples samples = {0};
    AppPressureTrendFit fit;

    samples.count = 7u;
    for (uint8_t i = 0u; i < samples.count; ++i) {
        samples.time_ms[i] = (uint32_t)i * 100u;
        samples.pressure_001mmhg[i] = 150000u - ((uint32_t)i * 100u);
    }
    if (AppPressureTrend_FitSamples(&samples, 600u, &fit) == 0u ||
        fit.slope_001mmhg_per_s != -1000 ||
        expect_u32("linear residual", fit.max_residual_001mmhg, 0u) != 0 ||
        expect_u32("linear prediction", fit.predicted_001mmhg, 149400u) != 0 ||
        AppPressureTrend_IsAcceptable(&fit, 500u, 1000u) == 0u ||
        AppPressureTrend_IsAcceptable(&fit, 500u, 999u) != 0u) {
        fputs("linear trend fit failed\n", stderr);
        return 1;
    }

    fit.max_residual_001mmhg = 500u;
    if (AppPressureTrend_IsAcceptable(&fit, 500u, 1000u) == 0u) {
        fputs("residual threshold handling failed\n", stderr);
        return 2;
    }
    fit.max_residual_001mmhg = 501u;
    if (AppPressureTrend_IsAcceptable(&fit, 500u, 1000u) != 0u) {
        fputs("excessive residual passed\n", stderr);
        return 6;
    }

    samples.count = 5u;
    if (AppPressureTrend_FitSamples(&samples, 1000u, &fit) != 0u) {
        fputs("insufficient sample count passed\n", stderr);
        return 3;
    }

    samples.count = 6u;
    for (uint8_t i = 0u; i < samples.count; ++i) {
        samples.time_ms[i] = (uint32_t)i * 99u;
        samples.pressure_001mmhg[i] = 100000u;
    }
    if (AppPressureTrend_FitSamples(&samples, 600u, &fit) != 0u) {
        fputs("short sample span passed\n", stderr);
        return 4;
    }

    samples.time_ms[5] = 500u;
    if (AppPressureTrend_FitSamples(&samples, 750u, &fit) == 0u ||
        fit.slope_001mmhg_per_s != 0 ||
        expect_u32("flat prediction", fit.predicted_001mmhg, 100000u) != 0 ||
        AppPressureTrend_IsAcceptable(&fit, 500u, 1000u) == 0u) {
        fputs("flat trend fit failed\n", stderr);
        return 5;
    }

    for (uint8_t i = 0u; i < samples.count; ++i) {
        samples.pressure_001mmhg[i] = 100000u + ((uint32_t)i * 100u);
    }
    if (AppPressureTrend_FitSamples(&samples, 750u, &fit) == 0u ||
        fit.slope_001mmhg_per_s <= 0 ||
        AppPressureTrend_IsAcceptable(&fit, 500u, 1000u) != 0u) {
        fputs("rising trend passed\n", stderr);
        return 8;
    }

    samples.count = 7u;
    for (uint8_t i = 0u; i < samples.count; ++i) {
        samples.time_ms[i] = 0xFFFFFF00u + ((uint32_t)i * 100u);
        samples.pressure_001mmhg[i] = 150000u - ((uint32_t)i * 100u);
    }
    if (AppPressureTrend_FitSamples(&samples, 0x00000158u, &fit) == 0u ||
        fit.slope_001mmhg_per_s != -1000 ||
        expect_u32("wrap prediction", fit.predicted_001mmhg, 149400u) != 0) {
        fputs("HAL tick wrap trend fit failed\n", stderr);
        return 7;
    }

    samples.count = APP_PRESSURE_TREND_MAX_SAMPLES + 1u;
    if (AppPressureTrend_FitSamples(&samples, 0u, &fit) != 0u) {
        fputs("sample count above fixed capacity passed\n", stderr);
        return 9;
    }

    puts("pressure trend smoke passed");
    return 0;
}
