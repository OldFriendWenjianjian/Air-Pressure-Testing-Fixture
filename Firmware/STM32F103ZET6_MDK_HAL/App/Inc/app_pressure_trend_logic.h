#ifndef APP_PRESSURE_TREND_LOGIC_H
#define APP_PRESSURE_TREND_LOGIC_H

#include <stdint.h>

#define APP_PRESSURE_TREND_MAX_SAMPLES 32u
#define APP_PRESSURE_TREND_MIN_SAMPLES 6u
#define APP_PRESSURE_TREND_MIN_SPAN_MS 500u

typedef struct {
    uint32_t time_ms[APP_PRESSURE_TREND_MAX_SAMPLES];
    uint32_t pressure_001mmhg[APP_PRESSURE_TREND_MAX_SAMPLES];
    uint8_t count;
} AppPressureTrendSamples;

typedef struct {
    int64_t slope_numerator;
    int64_t slope_denominator;
    int64_t sum_time_ms;
    int64_t sum_pressure_001mmhg;
    uint32_t time_origin_ms;
    int32_t slope_001mmhg_per_s;
    uint32_t predicted_001mmhg;
    uint32_t max_residual_001mmhg;
    uint32_t span_ms;
    uint8_t sample_count;
    uint8_t valid;
} AppPressureTrendFit;

static inline int64_t AppPressureTrend_DivRoundNearest(int64_t numerator,
                                                       int64_t denominator)
{
    if (denominator <= 0) {
        return 0;
    }
    if (numerator >= 0) {
        return (numerator + (denominator / 2)) / denominator;
    }
    return -(((-numerator) + (denominator / 2)) / denominator);
}

static inline uint32_t AppPressureTrend_ClampPressure(int64_t pressure_001mmhg)
{
    if (pressure_001mmhg <= 0) {
        return 0u;
    }
    if ((uint64_t)pressure_001mmhg > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)pressure_001mmhg;
}

static inline uint32_t AppPressureTrend_Predict(const AppPressureTrendFit *fit,
                                                 uint32_t time_ms)
{
    int64_t centered_time;
    int64_t numerator;
    int64_t denominator;

    if (fit == 0 || fit->valid == 0u || fit->sample_count == 0u ||
        fit->slope_denominator <= 0) {
        return 0u;
    }

    denominator = (int64_t)fit->sample_count * fit->slope_denominator;
    centered_time = ((int64_t)fit->sample_count *
                     (uint32_t)(time_ms - fit->time_origin_ms)) -
                    fit->sum_time_ms;
    numerator = (fit->sum_pressure_001mmhg * fit->slope_denominator) +
                (fit->slope_numerator * centered_time);
    return AppPressureTrend_ClampPressure(
        AppPressureTrend_DivRoundNearest(numerator, denominator));
}

static inline uint8_t AppPressureTrend_FitSamples(
    const AppPressureTrendSamples *samples,
    uint32_t prediction_time_ms,
    AppPressureTrendFit *fit)
{
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int64_t sum_xx = 0;
    int64_t sum_xy = 0;
    int64_t slope_numerator;
    int64_t slope_denominator;
    int64_t fit_denominator;
    uint32_t max_residual = 0u;
    uint32_t span_ms;
    uint32_t time_origin_ms;
    uint8_t count;

    if (fit == 0) {
        return 0u;
    }
    fit->slope_numerator = 0;
    fit->slope_denominator = 0;
    fit->sum_time_ms = 0;
    fit->sum_pressure_001mmhg = 0;
    fit->time_origin_ms = 0u;
    fit->slope_001mmhg_per_s = 0;
    fit->predicted_001mmhg = 0u;
    fit->max_residual_001mmhg = 0u;
    fit->span_ms = 0u;
    fit->sample_count = samples != 0 ? samples->count : 0u;
    fit->valid = 0u;

    if (samples == 0 || samples->count < APP_PRESSURE_TREND_MIN_SAMPLES ||
        samples->count > APP_PRESSURE_TREND_MAX_SAMPLES) {
        return 0u;
    }
    count = samples->count;
    time_origin_ms = samples->time_ms[0];
    span_ms = (uint32_t)(samples->time_ms[count - 1u] - time_origin_ms);
    if (span_ms < APP_PRESSURE_TREND_MIN_SPAN_MS) {
        return 0u;
    }

    for (uint8_t i = 0u; i < count; ++i) {
        const uint32_t relative_time =
            (uint32_t)(samples->time_ms[i] - time_origin_ms);
        const int64_t x = relative_time;
        const int64_t y = samples->pressure_001mmhg[i];

        if (i > 0u && relative_time <=
                (uint32_t)(samples->time_ms[i - 1u] - time_origin_ms)) {
            return 0u;
        }
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    slope_denominator = ((int64_t)count * sum_xx) - (sum_x * sum_x);
    if (slope_denominator <= 0) {
        return 0u;
    }
    slope_numerator = ((int64_t)count * sum_xy) - (sum_x * sum_y);
    fit_denominator = (int64_t)count * slope_denominator;

    for (uint8_t i = 0u; i < count; ++i) {
        const int64_t centered_time =
            ((int64_t)count *
             (uint32_t)(samples->time_ms[i] - time_origin_ms)) - sum_x;
        const int64_t fitted_numerator =
            (sum_y * slope_denominator) + (slope_numerator * centered_time);
        const int64_t measured_numerator =
            (int64_t)samples->pressure_001mmhg[i] * fit_denominator;
        const uint64_t residual_numerator = fitted_numerator >= measured_numerator ?
            (uint64_t)(fitted_numerator - measured_numerator) :
            (uint64_t)(measured_numerator - fitted_numerator);
        const uint64_t residual =
            (residual_numerator + (uint64_t)fit_denominator - 1u) /
            (uint64_t)fit_denominator;

        if (residual > max_residual) {
            max_residual = residual > UINT32_MAX ? UINT32_MAX : (uint32_t)residual;
        }
    }

    fit->slope_numerator = slope_numerator;
    fit->slope_denominator = slope_denominator;
    fit->sum_time_ms = sum_x;
    fit->sum_pressure_001mmhg = sum_y;
    fit->time_origin_ms = time_origin_ms;
    fit->slope_001mmhg_per_s = (int32_t)AppPressureTrend_DivRoundNearest(
        slope_numerator * 1000,
        slope_denominator);
    fit->max_residual_001mmhg = max_residual;
    fit->span_ms = span_ms;
    fit->sample_count = count;
    fit->valid = 1u;
    fit->predicted_001mmhg = AppPressureTrend_Predict(fit, prediction_time_ms);
    return 1u;
}

static inline uint8_t AppPressureTrend_IsAcceptable(
    const AppPressureTrendFit *fit,
    uint32_t max_residual_001mmhg,
    uint32_t max_drop_rate_001mmhg_per_s)
{
    uint64_t drop_rate = 0u;

    if (fit == 0 || fit->valid == 0u ||
        fit->max_residual_001mmhg > (uint64_t)max_residual_001mmhg ||
        fit->slope_001mmhg_per_s > 0) {
        return 0u;
    }
    if (fit->slope_001mmhg_per_s < 0) {
        drop_rate = (uint64_t)(-(int64_t)fit->slope_001mmhg_per_s);
    }
    return drop_rate <= (uint64_t)max_drop_rate_001mmhg_per_s ? 1u : 0u;
}

#endif
