#pragma once

#include <stdint.h>

typedef struct {
    uint32_t last_attempt_at;
    uint16_t event_count;
    uint16_t attempt_count;
    uint16_t success_count;
    uint8_t attempts_in_episode;
    uint8_t active;
} AppPressureMathSaturationRecovery;

static inline void AppPressureMathSaturation_Init(
    AppPressureMathSaturationRecovery *recovery)
{
    if (recovery == 0) {
        return;
    }
    recovery->last_attempt_at = 0u;
    recovery->event_count = 0u;
    recovery->attempt_count = 0u;
    recovery->success_count = 0u;
    recovery->attempts_in_episode = 0u;
    recovery->active = 0u;
}

static inline void AppPressureMathSaturation_Increment(uint16_t *value)
{
    if (value != 0 && *value < UINT16_MAX) {
        (*value)++;
    }
}

static inline uint8_t AppPressureMathSaturation_RecordFault(
    AppPressureMathSaturationRecovery *recovery,
    uint32_t now,
    uint32_t retry_interval_ms,
    uint8_t max_attempts)
{
    if (recovery == 0 || max_attempts == 0u) {
        return 0u;
    }
    if (recovery->active == 0u) {
        recovery->active = 1u;
        recovery->attempts_in_episode = 0u;
        recovery->last_attempt_at = now;
        AppPressureMathSaturation_Increment(&recovery->event_count);
    }
    if (recovery->attempts_in_episode >= max_attempts) {
        return 0u;
    }
    if (recovery->attempts_in_episode != 0u &&
        (now - recovery->last_attempt_at) < retry_interval_ms) {
        return 0u;
    }

    recovery->attempts_in_episode++;
    recovery->last_attempt_at = now;
    AppPressureMathSaturation_Increment(&recovery->attempt_count);
    return 1u;
}

static inline uint8_t AppPressureMathSaturation_RecordRecovery(
    AppPressureMathSaturationRecovery *recovery)
{
    if (recovery == 0 || recovery->active == 0u) {
        return 0u;
    }
    AppPressureMathSaturation_Increment(&recovery->success_count);
    recovery->last_attempt_at = 0u;
    recovery->attempts_in_episode = 0u;
    recovery->active = 0u;
    return 1u;
}
