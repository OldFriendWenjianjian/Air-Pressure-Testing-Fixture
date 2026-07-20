#ifndef APP_PRESSURE_SETTLE_LOGIC_H
#define APP_PRESSURE_SETTLE_LOGIC_H

#include <stdint.h>

typedef enum {
    APP_PRESSURE_SETTLE_APPROACH_FILL = 0u,
    APP_PRESSURE_SETTLE_APPROACH_RELIEF,
    APP_PRESSURE_SETTLE_APPROACH_SETTLE
} AppPressureSettleApproachAction;

typedef enum {
    APP_PRESSURE_SETTLE_FAILURE_NONE = 0u,
    APP_PRESSURE_SETTLE_FAILURE_SENSOR_FAULT,
    APP_PRESSURE_SETTLE_FAILURE_SENSOR_INVALID,
    APP_PRESSURE_SETTLE_FAILURE_OVERPRESSURE,
    APP_PRESSURE_SETTLE_FAILURE_NO_PRESSURE_RISE,
    APP_PRESSURE_SETTLE_FAILURE_TIMEOUT,
    APP_PRESSURE_SETTLE_FAILURE_TREND_SAMPLES,
    APP_PRESSURE_SETTLE_FAILURE_TREND_RESIDUAL,
    APP_PRESSURE_SETTLE_FAILURE_TREND_DROP_RATE,
    APP_PRESSURE_SETTLE_FAILURE_TREND_DIRECTION
} AppPressureSettleFailureReason;

static inline uint8_t AppPressureSettle_FailureCanContinue(uint8_t failure_reason)
{
    return failure_reason == APP_PRESSURE_SETTLE_FAILURE_NO_PRESSURE_RISE ||
           failure_reason == APP_PRESSURE_SETTLE_FAILURE_TIMEOUT ||
           failure_reason == APP_PRESSURE_SETTLE_FAILURE_TREND_SAMPLES ||
           failure_reason == APP_PRESSURE_SETTLE_FAILURE_TREND_RESIDUAL ||
           failure_reason == APP_PRESSURE_SETTLE_FAILURE_TREND_DROP_RATE ||
           failure_reason == APP_PRESSURE_SETTLE_FAILURE_TREND_DIRECTION ? 1u : 0u;
}

static inline AppPressureSettleApproachAction AppPressureSettle_SelectApproach(
    uint32_t current_001mmhg,
    uint32_t target_001mmhg,
    uint32_t overshoot_tolerance_001mmhg)
{
    const uint64_t upper_001mmhg =
        (uint64_t)target_001mmhg + overshoot_tolerance_001mmhg;

    if (current_001mmhg < target_001mmhg) {
        return APP_PRESSURE_SETTLE_APPROACH_FILL;
    }
    return (uint64_t)current_001mmhg > upper_001mmhg ?
           APP_PRESSURE_SETTLE_APPROACH_RELIEF :
           APP_PRESSURE_SETTLE_APPROACH_SETTLE;
}

static inline uint32_t AppPressureSettle_DropRate001mmHgPerSecond(
    uint32_t window_start_001mmhg,
    uint32_t current_001mmhg,
    uint32_t elapsed_ms)
{
    uint64_t scaled_drop;

    if (elapsed_ms == 0u || current_001mmhg >= window_start_001mmhg) {
        return 0u;
    }
    scaled_drop = (uint64_t)(window_start_001mmhg - current_001mmhg) * 1000u;
    return scaled_drop / elapsed_ms > UINT32_MAX ?
           UINT32_MAX : (uint32_t)(scaled_drop / elapsed_ms);
}

static inline uint8_t AppPressureSettle_WindowReady(
    uint32_t window_start_001mmhg,
    uint32_t current_001mmhg,
    uint32_t target_001mmhg,
    uint32_t overshoot_tolerance_001mmhg,
    uint32_t elapsed_ms,
    uint32_t required_wait_ms,
    uint32_t max_drop_rate_001mmhg_per_s)
{
    uint64_t scaled_drop;
    uint64_t allowed_drop;

    const uint64_t upper_001mmhg =
        (uint64_t)target_001mmhg + overshoot_tolerance_001mmhg;

    if (current_001mmhg < target_001mmhg ||
        (uint64_t)current_001mmhg > upper_001mmhg ||
        elapsed_ms < required_wait_ms) {
        return 0u;
    }
    if (current_001mmhg >= window_start_001mmhg) {
        return 1u;
    }
    scaled_drop = (uint64_t)(window_start_001mmhg - current_001mmhg) * 1000u;
    allowed_drop = (uint64_t)max_drop_rate_001mmhg_per_s * elapsed_ms;
    return scaled_drop <= allowed_drop ? 1u : 0u;
}

#endif
