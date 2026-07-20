#include "app_sensor_calibration.h"

#include "app_config.h"
#include "app_pressure.h"
#include "app_valves.h"
#include "main.h"

#define SENSOR_CAL_CAPTURE_COMPLETE_MASK     0x0Fu

#define SENSOR_CAL_FLAG_ACTIVE               0x01u
#define SENSOR_CAL_FLAG_SOURCE_VALID         0x02u
#define SENSOR_CAL_FLAG_SOURCE_FAULT         0x04u
#define SENSOR_CAL_FLAG_STORAGE_LOADED       0x08u
#define SENSOR_CAL_FLAG_STAGED_COMPLETE      0x10u
#define SENSOR_CAL_FLAG_ZERO_READY           0x20u
#define SENSOR_CAL_FLAG_AUTO_VENT_ACTIVE     0x40u
#define SENSOR_CAL_FLAG_STORAGE_FAULT        0x80u

typedef struct {
    AppPressureCalibrationProfile staged;
    uint32_t samples[APP_SENSOR_CAL_LIVE_SAMPLE_COUNT];
    uint32_t sample_sum;
    uint32_t last_sample_sequence;
    uint32_t actuator_started_at;
    uint32_t vent_stable_since;
    uint16_t actuator_lease_ms;
    uint8_t sample_count;
    uint8_t sample_write_index;
    uint8_t active;
    uint8_t actuator;
    uint8_t captured_mask;
    uint8_t zero_ready;
    uint8_t last_detail;
    uint8_t source_index;
    uint8_t destination_slot;
    uint8_t in_place_mode;
} SensorCalibrationContext;

static SensorCalibrationContext s_cal;
static AppSensorCalibrationRoute s_route;

static PressureSensorIndex source_sensor(void)
{
    return (PressureSensorIndex)s_cal.source_index;
}

static void reset_samples(void)
{
    s_cal.sample_sum = 0u;
    s_cal.sample_count = 0u;
    s_cal.sample_write_index = 0u;
    s_cal.last_sample_sequence = AppPressure_GetSampleSequence(source_sensor());
    for (uint8_t i = 0u; i < APP_SENSOR_CAL_LIVE_SAMPLE_COUNT; ++i) {
        s_cal.samples[i] = 0u;
    }
}

static uint32_t live_raw_average(void)
{
    if (s_cal.sample_count == 0u) {
        return AppPressure_GetRaw(source_sensor());
    }
    return (s_cal.sample_sum + (s_cal.sample_count / 2u)) / s_cal.sample_count;
}

static void close_calibration_valves(void)
{
    if (s_cal.source_index < APP_TANK_COUNT) {
        AppValves_Set(s_route.inlet_valve, 0u);
        AppValves_Set(s_route.outlet_valve, 0u);
        AppValves_Set(s_route.relief_valve, 0u);
    } else {
        AppValves_Set(s_route.inlet_valve, 0u);
        AppValves_Set(s_route.outlet_valve, 0u);
        AppValves_Set(s_route.relief_valve, 0u);
        AppValves_Set(s_route.channel_valve, 0u);
    }
}

static void apply_actuator(void)
{
    close_calibration_valves();
    if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_STOP) {
        return;
    }
    if (s_cal.source_index < APP_TANK_COUNT) {
        if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_FILL) {
            AppValves_Set(s_route.inlet_valve, 1u);
        } else {
            AppValves_Set(s_route.relief_valve, 1u);
        }
    } else {
        AppValves_Set(s_route.outlet_valve, 1u);
        AppValves_Set(s_route.channel_valve, 1u);
        if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_FILL) {
            AppValves_Set(s_route.inlet_valve, 1u);
        } else {
            AppValves_Set(s_route.relief_valve, 1u);
        }
    }
}

static uint8_t source_path_valid(void)
{
    if (!AppPressure_IsValid(source_sensor())) {
        return 0u;
    }
    if (s_cal.in_place_mode != 0u && s_cal.source_index >= PRESSURE_SENSOR_CH1 &&
        !AppPressure_IsValid(PRESSURE_SENSOR_TANK_50)) {
        return 0u;
    }
    return 1u;
}

static uint8_t source_path_overpressure(void)
{
    if (AppPressure_GetSafety001mmHg(source_sensor()) >= APP_SENSOR_CAL_HARD_LIMIT_001MMHG) {
        return 1u;
    }
    if (s_cal.in_place_mode != 0u && s_cal.source_index >= PRESSURE_SENSOR_CH1 &&
        AppPressure_GetSafety001mmHg(PRESSURE_SENSOR_TANK_50) >=
            APP_SENSOR_CAL_HARD_LIMIT_001MMHG) {
        return 1u;
    }
    return 0u;
}

static uint8_t source_path_is_vented(void)
{
    if (!source_path_valid() ||
        AppPressure_GetNominal001mmHg(source_sensor()) >
            APP_SENSOR_CAL_VENT_ZERO_MAX_001MMHG) {
        return 0u;
    }
    if (s_cal.in_place_mode != 0u && s_cal.source_index >= PRESSURE_SENSOR_CH1 &&
        AppPressure_GetNominal001mmHg(PRESSURE_SENSOR_TANK_50) >
            APP_SENSOR_CAL_VENT_ZERO_MAX_001MMHG) {
        return 0u;
    }
    return 1u;
}

static void stop_actuator(uint8_t reset_window)
{
    s_cal.actuator = APP_SENSOR_CAL_ACTUATOR_STOP;
    s_cal.actuator_lease_ms = 0u;
    s_cal.actuator_started_at = 0u;
    s_cal.vent_stable_since = 0u;
    close_calibration_valves();
    if (reset_window != 0u) {
        reset_samples();
    }
}

static void clear_staging(void)
{
    s_cal.captured_mask = 0u;
    for (uint8_t anchor = 0u; anchor < APP_PRESSURE_CAL_ANCHOR_COUNT; ++anchor) {
        s_cal.staged.raw[anchor] = 0u;
        s_cal.staged.pressure_001mmhg[anchor] = 0u;
    }
}

static void begin_auto_vent(uint8_t detail)
{
    s_cal.actuator = APP_SENSOR_CAL_ACTUATOR_AUTO_VENT;
    s_cal.actuator_started_at = HAL_GetTick();
    s_cal.actuator_lease_ms = 0u;
    s_cal.vent_stable_since = 0u;
    s_cal.zero_ready = 0u;
    s_cal.last_detail = detail;
    reset_samples();
    apply_actuator();
}

static void collect_live_sample(void)
{
    const uint32_t sequence = AppPressure_GetSampleSequence(source_sensor());
    uint32_t raw;

    if (!AppPressure_IsValid(source_sensor()) || sequence == s_cal.last_sample_sequence) {
        return;
    }
    s_cal.last_sample_sequence = sequence;
    raw = AppPressure_GetRaw(source_sensor());

    if (s_cal.sample_count < APP_SENSOR_CAL_LIVE_SAMPLE_COUNT) {
        s_cal.samples[s_cal.sample_write_index] = raw;
        s_cal.sample_sum += raw;
        s_cal.sample_count++;
    } else {
        s_cal.sample_sum -= s_cal.samples[s_cal.sample_write_index];
        s_cal.samples[s_cal.sample_write_index] = raw;
        s_cal.sample_sum += raw;
    }
    s_cal.sample_write_index++;
    if (s_cal.sample_write_index >= APP_SENSOR_CAL_LIVE_SAMPLE_COUNT) {
        s_cal.sample_write_index = 0u;
    }
}

void AppSensorCalibration_Init(void)
{
    s_cal.active = 0u;
    s_cal.actuator = APP_SENSOR_CAL_ACTUATOR_STOP;
    s_cal.zero_ready = 0u;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
    s_cal.source_index = 0u;
    s_cal.destination_slot = 0u;
    s_cal.in_place_mode = 0u;
    s_cal.actuator_started_at = 0u;
    s_cal.actuator_lease_ms = 0u;
    s_cal.vent_stable_since = 0u;
    clear_staging();
    reset_samples();
}

int AppSensorCalibration_Enter(uint8_t in_place_mode, uint8_t destination_slot)
{
    if (AppSensorCalibration_ResolveRoute(in_place_mode, destination_slot, &s_route) != 0) {
        return -1;
    }
    AppValves_AllClosed();
    s_cal.in_place_mode = s_route.in_place_mode;
    s_cal.destination_slot = s_route.destination_slot;
    s_cal.source_index = s_route.source_index;
    AppPressure_SetScanMask(s_route.scan_mask);
    s_cal.active = 1u;
    s_cal.zero_ready = 0u;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
    clear_staging();
    stop_actuator(1u);
    return 0;
}

void AppSensorCalibration_Exit(void)
{
    stop_actuator(0u);
    s_cal.active = 0u;
    s_cal.zero_ready = 0u;
    AppPressure_SetScanMask(0x3FFFu);
}

void AppSensorCalibration_Task(void)
{
    const uint32_t now = HAL_GetTick();

    if (s_cal.active == 0u) {
        close_calibration_valves();
        return;
    }

    collect_live_sample();
    if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_FILL) {
        if (!source_path_valid()) {
            stop_actuator(1u);
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_SOURCE_INVALID;
            return;
        }
        if (source_path_overpressure()) {
            begin_auto_vent(APP_SENSOR_CAL_DETAIL_OVERPRESSURE);
            return;
        }
        if ((now - s_cal.actuator_started_at) >= s_cal.actuator_lease_ms) {
            stop_actuator(1u);
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_LEASE_EXPIRED;
            return;
        }
    } else if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_RELEASE) {
        if ((now - s_cal.actuator_started_at) >= s_cal.actuator_lease_ms) {
            stop_actuator(1u);
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_LEASE_EXPIRED;
            return;
        }
    } else if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_AUTO_VENT) {
        if (source_path_is_vented()) {
            if (s_cal.vent_stable_since == 0u) {
                s_cal.vent_stable_since = now;
            } else if ((now - s_cal.vent_stable_since) >= APP_SENSOR_CAL_VENT_STABLE_MS) {
                stop_actuator(1u);
                s_cal.zero_ready = 1u;
                s_cal.last_detail = APP_SENSOR_CAL_DETAIL_VENT_COMPLETE;
                return;
            }
        } else {
            s_cal.vent_stable_since = 0u;
        }
        if ((now - s_cal.actuator_started_at) >= APP_SENSOR_CAL_VENT_TIMEOUT_MS) {
            stop_actuator(1u);
            s_cal.zero_ready = 0u;
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_VENT_TIMEOUT;
            return;
        }
    }

    apply_actuator();
}

int AppSensorCalibration_Jog(uint8_t actuator, uint16_t lease_ms)
{
    const uint32_t now = HAL_GetTick();

    if (s_cal.active == 0u) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    if (actuator == APP_SENSOR_CAL_ACTUATOR_STOP) {
        stop_actuator(1u);
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
        return 0;
    }
    if ((actuator != APP_SENSOR_CAL_ACTUATOR_FILL &&
         actuator != APP_SENSOR_CAL_ACTUATOR_RELEASE) || lease_ms == 0u) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_VALUE;
        return -1;
    }
    if (actuator == APP_SENSOR_CAL_ACTUATOR_FILL) {
        if (!source_path_valid()) {
            stop_actuator(1u);
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_SOURCE_INVALID;
            return -1;
        }
        if (source_path_overpressure()) {
            begin_auto_vent(APP_SENSOR_CAL_DETAIL_OVERPRESSURE);
            return -1;
        }
        s_cal.zero_ready = 0u;
    }
    if (lease_ms > APP_SENSOR_CAL_JOG_MAX_LEASE_MS) {
        lease_ms = APP_SENSOR_CAL_JOG_MAX_LEASE_MS;
    }
    if (s_cal.actuator != actuator) {
        reset_samples();
    }
    s_cal.actuator = actuator;
    s_cal.actuator_started_at = now;
    s_cal.actuator_lease_ms = lease_ms;
    s_cal.vent_stable_since = 0u;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
    apply_actuator();
    return 0;
}

int AppSensorCalibration_StartAutoVent(void)
{
    if (s_cal.active == 0u) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    begin_auto_vent(APP_SENSOR_CAL_DETAIL_OK);
    return 0;
}

int AppSensorCalibration_CancelAutoVent(void)
{
    if (s_cal.active == 0u || s_cal.actuator != APP_SENSOR_CAL_ACTUATOR_AUTO_VENT) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    stop_actuator(1u);
    s_cal.zero_ready = 0u;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
    return 0;
}

int AppSensorCalibration_Record(uint8_t point_index, uint32_t actual_001mmhg)
{
    const uint32_t raw = live_raw_average();

    if (s_cal.active == 0u || s_cal.actuator != APP_SENSOR_CAL_ACTUATOR_STOP) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    if (!AppPressure_IsValid(source_sensor())) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_SOURCE_INVALID;
        return -1;
    }
    if (s_cal.sample_count < APP_SENSOR_CAL_LIVE_SAMPLE_COUNT) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_NOT_ENOUGH_SAMPLES;
        return -1;
    }
    if (point_index >= APP_PRESSURE_CAL_ANCHOR_COUNT ||
        (point_index == 0u && actual_001mmhg != 0u) ||
        (point_index > 0u &&
         (actual_001mmhg == 0u || actual_001mmhg >= APP_SENSOR_CAL_HARD_LIMIT_001MMHG))) {
        s_cal.last_detail = point_index == 0u ? APP_SENSOR_CAL_DETAIL_ZERO_VALUE_INVALID :
                                                APP_SENSOR_CAL_DETAIL_BAD_VALUE;
        return -1;
    }

    for (uint8_t i = 0u; i < APP_PRESSURE_CAL_ANCHOR_COUNT; ++i) {
        if (i == point_index || (s_cal.captured_mask & (1u << i)) == 0u) {
            continue;
        }
        if ((i < point_index &&
             (s_cal.staged.raw[i] >= raw ||
              s_cal.staged.pressure_001mmhg[i] >= actual_001mmhg)) ||
            (i > point_index &&
             (s_cal.staged.raw[i] <= raw ||
              s_cal.staged.pressure_001mmhg[i] <= actual_001mmhg))) {
            s_cal.last_detail = APP_SENSOR_CAL_DETAIL_PROFILE_INVALID;
            return -1;
        }
    }

    s_cal.staged.raw[point_index] = raw;
    s_cal.staged.pressure_001mmhg[point_index] = actual_001mmhg;
    s_cal.captured_mask |= (uint8_t)1u << point_index;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_CAPTURED;
    return 0;
}

int AppSensorCalibration_SaveSlot(uint8_t slot)
{
    if (s_cal.active == 0u || s_cal.actuator != APP_SENSOR_CAL_ACTUATOR_STOP) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    if (slot >= APP_PRESSURE_SENSOR_COUNT || slot != s_cal.destination_slot ||
        s_cal.captured_mask != SENSOR_CAL_CAPTURE_COMPLETE_MASK ||
        AppPressureCalibration_ValidateProfile(&s_cal.staged) != 0) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_PROFILE_INVALID;
        return -1;
    }
    AppValves_AllClosed();
    if (AppPressureCalibration_SaveProfile(slot, &s_cal.staged) != 0) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_STORAGE_ERROR;
        return -1;
    }
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_SAVED;
    return 0;
}

int AppSensorCalibration_ClearSlot(uint8_t slot)
{
    if (s_cal.active == 0u || s_cal.actuator != APP_SENSOR_CAL_ACTUATOR_STOP ||
        slot >= APP_PRESSURE_SENSOR_COUNT) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    AppValves_AllClosed();
    if (AppPressureCalibration_ClearProfile(slot) != 0) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_STORAGE_ERROR;
        return -1;
    }
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_CLEARED;
    return 0;
}

int AppSensorCalibration_ResetSession(void)
{
    if (s_cal.active == 0u) {
        s_cal.last_detail = APP_SENSOR_CAL_DETAIL_BAD_STATE;
        return -1;
    }
    stop_actuator(1u);
    clear_staging();
    s_cal.zero_ready = 0u;
    s_cal.last_detail = APP_SENSOR_CAL_DETAIL_OK;
    return 0;
}

void AppSensorCalibration_GetStatus(uint8_t selected_slot,
                                    AppSensorCalibrationStatus *status)
{
    const uint8_t source_valid = source_path_valid();
    const uint8_t source_fault =
        AppPressure_GetFaultCode(source_sensor()) != APP_PRESSURE_FAULT_NONE ||
        (s_cal.in_place_mode != 0u && s_cal.source_index >= PRESSURE_SENSOR_CH1 &&
         AppPressure_GetFaultCode(PRESSURE_SENSOR_TANK_50) != APP_PRESSURE_FAULT_NONE) ? 1u : 0u;

    if (status == 0) {
        return;
    }
    status->flags = 0u;
    status->actuator = s_cal.actuator;
    status->calibrated_mask = AppPressureCalibration_GetMask();
    status->selected_slot = selected_slot;
    status->in_place_mode = s_cal.in_place_mode;
    status->last_detail = s_cal.last_detail;
    status->live_raw_average = live_raw_average();
    status->live_nominal_001mmhg = AppPressure_GetNominal001mmHg(source_sensor());

    if (selected_slot > 0u && selected_slot <= APP_PRESSURE_SENSOR_COUNT &&
        AppPressureCalibration_GetProfile((uint8_t)(selected_slot - 1u), &status->profile) == 0) {
        status->captured_mask = SENSOR_CAL_CAPTURE_COMPLETE_MASK;
    } else {
        status->selected_slot = (uint8_t)(s_cal.destination_slot + 1u);
        status->captured_mask = s_cal.captured_mask;
        status->profile = s_cal.staged;
    }

    if (s_cal.active != 0u) {
        status->flags |= SENSOR_CAL_FLAG_ACTIVE;
    }
    if (source_valid != 0u) {
        status->flags |= SENSOR_CAL_FLAG_SOURCE_VALID;
    } else if (source_fault != 0u) {
        status->flags |= SENSOR_CAL_FLAG_SOURCE_FAULT;
    }
    if (AppPressureCalibration_IsStorageLoaded() != 0u) {
        status->flags |= SENSOR_CAL_FLAG_STORAGE_LOADED;
    }
    if (status->captured_mask == SENSOR_CAL_CAPTURE_COMPLETE_MASK) {
        status->flags |= SENSOR_CAL_FLAG_STAGED_COMPLETE;
    }
    if (s_cal.zero_ready != 0u) {
        status->flags |= SENSOR_CAL_FLAG_ZERO_READY;
    }
    if (s_cal.actuator == APP_SENSOR_CAL_ACTUATOR_AUTO_VENT) {
        status->flags |= SENSOR_CAL_FLAG_AUTO_VENT_ACTIVE;
    }
    if (AppPressureCalibration_HasStorageFault() != 0u) {
        status->flags |= SENSOR_CAL_FLAG_STORAGE_FAULT;
    }
}

uint8_t AppSensorCalibration_IsActive(void)
{
    return s_cal.active;
}

uint8_t AppSensorCalibration_GetActuator(void)
{
    return s_cal.actuator;
}
