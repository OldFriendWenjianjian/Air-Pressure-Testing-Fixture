#pragma once

#include <stdint.h>

typedef struct {
    uint8_t consecutive_valid_samples;
} AppPressureHotplugRecovery;

static inline void AppPressureHotplug_Reset(AppPressureHotplugRecovery *recovery)
{
    if (recovery != 0) {
        recovery->consecutive_valid_samples = 0u;
    }
}

static inline uint8_t AppPressureHotplug_RecordValid(
    AppPressureHotplugRecovery *recovery,
    uint8_t required_samples)
{
    if (recovery == 0 || required_samples == 0u) {
        return 0u;
    }
    if (recovery->consecutive_valid_samples < required_samples) {
        recovery->consecutive_valid_samples++;
    }
    return recovery->consecutive_valid_samples >= required_samples ? 1u : 0u;
}
