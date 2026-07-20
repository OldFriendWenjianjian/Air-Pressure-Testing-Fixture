#ifndef APP_PRESSURE_VENT_LOGIC_H
#define APP_PRESSURE_VENT_LOGIC_H

#include <stdint.h>

#include "app_pressure.h"

typedef enum {
    APP_PRESSURE_VENT_PHASE_OPEN = 0,
    APP_PRESSURE_VENT_PHASE_STATIC_SETTLE
} AppPressureVentPhase;

typedef enum {
    APP_PRESSURE_VENT_RELIEF_CLOSED = 0,
    APP_PRESSURE_VENT_RELIEF_OPEN,
    APP_PRESSURE_VENT_ZERO_CONFIRMED
} AppPressureVentAction;

typedef struct {
    uint32_t zero_max_001mmhg;
    uint32_t math_saturation_release_001mmhg;
    uint32_t minimum_open_ms;
    uint32_t zero_hold_open_ms;
    uint32_t static_settle_ms;
    uint8_t static_valid_samples;
} AppPressureVentLimits;

typedef struct {
    uint32_t phase_started_at;
    uint32_t zero_window_started_at;
    uint32_t last_sample_sequence;
    uint8_t phase;
    uint8_t zero_window_active;
    uint8_t static_valid_samples;
} AppPressureVentController;

static inline void AppPressureVent_Init(AppPressureVentController *controller,
                                        uint32_t now,
                                        uint32_t sample_sequence)
{
    if (controller == 0) {
        return;
    }
    controller->phase_started_at = now;
    controller->zero_window_started_at = now;
    controller->last_sample_sequence = sample_sequence;
    controller->phase = APP_PRESSURE_VENT_PHASE_OPEN;
    controller->zero_window_active = 0u;
    controller->static_valid_samples = 0u;
}

static inline AppPressureVentAction AppPressureVent_Step(
    AppPressureVentController *controller,
    const AppPressureVentLimits *limits,
    uint32_t now,
    uint32_t sample_sequence,
    uint8_t pressure_valid,
    uint8_t has_valid_history,
    uint8_t fault_code,
    uint32_t measured_001mmhg,
    uint32_t last_safe_001mmhg)
{
    uint8_t pressure_at_or_below_zero = 0u;

    if (controller == 0 || limits == 0 || limits->static_valid_samples == 0u) {
        return APP_PRESSURE_VENT_RELIEF_CLOSED;
    }

    if (controller->phase == APP_PRESSURE_VENT_PHASE_OPEN) {
        if (pressure_valid != 0u) {
            pressure_at_or_below_zero =
                measured_001mmhg <= limits->zero_max_001mmhg ? 1u : 0u;
        } else if (fault_code == APP_PRESSURE_FAULT_MATH_SATURATION &&
                   has_valid_history != 0u &&
                   last_safe_001mmhg <= limits->math_saturation_release_001mmhg) {
            pressure_at_or_below_zero = 1u;
        }

        if (pressure_at_or_below_zero != 0u) {
            if (controller->zero_window_active == 0u) {
                controller->zero_window_active = 1u;
                controller->zero_window_started_at = now;
            }
        } else {
            controller->zero_window_active = 0u;
        }

        if ((now - controller->phase_started_at) >= limits->minimum_open_ms &&
            controller->zero_window_active != 0u &&
            (now - controller->zero_window_started_at) >= limits->zero_hold_open_ms) {
            controller->phase = APP_PRESSURE_VENT_PHASE_STATIC_SETTLE;
            controller->phase_started_at = now;
            controller->last_sample_sequence = sample_sequence;
            controller->static_valid_samples = 0u;
            controller->zero_window_active = 0u;
            return APP_PRESSURE_VENT_RELIEF_CLOSED;
        }
        return APP_PRESSURE_VENT_RELIEF_OPEN;
    }

    if (pressure_valid == 0u) {
        controller->static_valid_samples = 0u;
        return APP_PRESSURE_VENT_RELIEF_CLOSED;
    }
    if ((now - controller->phase_started_at) < limits->static_settle_ms ||
        sample_sequence == controller->last_sample_sequence) {
        return APP_PRESSURE_VENT_RELIEF_CLOSED;
    }

    controller->last_sample_sequence = sample_sequence;
    if (measured_001mmhg > limits->zero_max_001mmhg) {
        controller->phase = APP_PRESSURE_VENT_PHASE_OPEN;
        controller->phase_started_at = now;
        controller->zero_window_started_at = now;
        controller->zero_window_active = 0u;
        controller->static_valid_samples = 0u;
        return APP_PRESSURE_VENT_RELIEF_OPEN;
    }

    if (controller->static_valid_samples < limits->static_valid_samples) {
        ++controller->static_valid_samples;
    }
    return controller->static_valid_samples >= limits->static_valid_samples ?
           APP_PRESSURE_VENT_ZERO_CONFIRMED :
           APP_PRESSURE_VENT_RELIEF_CLOSED;
}

#endif
