#include "app_state_machine.h"
#include "app_config.h"
#include "app_current.h"
#include "app_keys.h"
#include "app_pcba_uart.h"
#include "app_power.h"
#include "app_pressure.h"
#include "app_pressure_calibration.h"
#include "app_pressure_scope_logic.h"
#include "app_pressure_settle_logic.h"
#include "app_pressure_trend_logic.h"
#include "app_pressure_vent_logic.h"
#include "app_protocol.h"
#include "app_sensor_calibration.h"
#include "app_usb_control.h"
#include "app_valves.h"
#include "board_pins.h"
#include "main.h"
#include "usb_msc_app.h"

typedef struct {
    AppRuntimeState state;
    uint32_t entered_at;
    uint8_t step_sent;
    uint32_t airtight_start[APP_TANK_COUNT];
    uint32_t pcba_test_pressure[APP_PCBA_CHANNEL_COUNT];
    uint8_t pcba_online[APP_PCBA_CHANNEL_COUNT];
    uint8_t pcba_low_power_ok[APP_PCBA_CHANNEL_COUNT];
    uint8_t pcba_normal_power_ok[APP_PCBA_CHANNEL_COUNT];
    uint8_t running;
    uint8_t paused;
    uint8_t manual_mode;
    uint8_t pcba_current_50ma_enabled;
    uint8_t pcba_supply_5v_enabled;
    uint8_t single_pcba_mode;
    uint32_t manual_valve_override_mask;
    uint32_t manual_valve_open_mask;
    uint32_t pressure_tolerance_001mmhg;
    uint32_t single_tank_pcba_trend_max_residual_001mmhg;
    uint32_t single_tank_pcba_trend_window_ms;
    uint32_t single_tank_pcba_max_drop_rate_001mmhg_per_s;
    uint8_t single_tank_active;
    uint8_t single_tank_index;
    uint32_t single_tank_target_001mmhg;
    uint32_t single_tank_tolerance_001mmhg;
    uint32_t single_tank_phase_started_at;
    uint32_t single_tank_last_sample_at;
    uint32_t single_tank_sample_accum_001mmhg;
    uint8_t single_tank_sample_count;
    uint32_t single_tank_last_settled_pressure_001mmhg;
    uint8_t single_tank_last_settled_valid;
    uint8_t single_tank_last_pulse_mode;
    uint32_t single_tank_last_refill_reference_001mmhg;
    uint32_t single_tank_last_refill_progress_at;
    uint8_t single_tank_last_refill_reference_valid;
    uint8_t single_tank_refill_no_rise_count;
    uint8_t single_tank_protection_active;
    uint8_t single_tank_protection_reason;
    uint8_t single_tank_protection_tank_index;
    uint8_t single_tank_protection_sensor_index;
    uint8_t single_tank_protection_inlet_valve;
} AppContext;

static AppContext s_app;
static uint32_t s_pressure_invalid_since[APP_PRESSURE_SENSOR_COUNT];
static AppPcbaTimingReport s_pcba_timing_report;
static AppSingleTankPcbaReport s_single_tank_pcba_report;
static uint8_t s_pcba_timing_requested;
static uint8_t s_single_tank_pcba_requested;
static uint8_t s_single_tank_pcba_continue_on_fail;
static uint32_t s_pcba_timing_pass_count;
static uint32_t s_pcba_timing_fail_count;
static uint8_t s_pcba_timing_stop_on_fail;

#define APP_PCBA_PROBE_SERVICE_INTERVAL 5000u
#define APP_PCBA_TIMING_PASS_LIMIT_US   10000u
#define SINGLE_TANK_PCBA_FLAG_5V        0x01u
#define SINGLE_TANK_PCBA_FLAG_45V       0x02u
#define SINGLE_TANK_PCBA_FLAG_50MA      0x04u
#define SINGLE_TANK_PCBA_FLAG_CURRENT   0x08u

static const uint8_t s_channel_valves[8] = {13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u};
typedef struct {
    PressureSensorIndex sensor;
    uint8_t inlet_valve;
    uint8_t outlet_valve;
    uint8_t relief_valve;
    uint32_t default_target_001mmhg;
} TankLoopSpec;

typedef enum {
    SINGLE_TANK_PULSE_NONE = 0u,
    SINGLE_TANK_PULSE_REFILL,
    SINGLE_TANK_PULSE_RELIEF
} SingleTankPulseMode;

typedef enum {
    SINGLE_TANK_PHASE_IDLE = 0u,
    SINGLE_TANK_PHASE_FAST_REFILL,
    SINGLE_TANK_PHASE_PULSE_REFILL,
    SINGLE_TANK_PHASE_PULSE_RELIEF,
    SINGLE_TANK_PHASE_SETTLING,
    SINGLE_TANK_PHASE_SAMPLING,
    SINGLE_TANK_PHASE_PROTECTED
} SingleTankLoopPhase;

typedef enum {
    SINGLE_TANK_PCBA_PRESSURE_ABORT = 0u,
    SINGLE_TANK_PCBA_PRESSURE_SKIPPED,
    SINGLE_TANK_PCBA_PRESSURE_READY
} SingleTankPcbaPressureStepResult;

typedef struct {
    AppPressureTrendFit fit;
    uint32_t last_pressure_001mmhg;
} SingleTankPcbaTrendCapture;

static const TankLoopSpec s_tank_loop_specs[APP_TANK_COUNT] = {
    {PRESSURE_SENSOR_TANK_50,  1u,  2u, 21u, APP_PRESSURE_50_MMHG},
    {PRESSURE_SENSOR_TANK_150, 3u,  4u, 22u, APP_PRESSURE_150_MMHG},
    {PRESSURE_SENSOR_TANK_250, 5u,  6u, 23u, APP_PRESSURE_250_MMHG},
    {PRESSURE_SENSOR_TANK_100, 7u,  8u, 24u, APP_PRESSURE_100_MMHG},
    {PRESSURE_SENSOR_TANK_200, 9u, 10u, 25u, APP_PRESSURE_200_MMHG},
    {PRESSURE_SENSOR_TANK_285, 11u, 12u, 26u, APP_PRESSURE_285_MMHG}
};
static const char *const s_state_names[APP_STATE_COUNT] = {
    "USB MSC",
    "Init tanks",
    "Auto airtightness",
    "Ready",
    "PCBA power on",
    "Standby current",
    "Wake PCBA",
    "Work current",
    "Set test mode",
    "Record zero",
    "Switch 4.5V",
    "Low power query",
    "Switch 5V",
    "Normal power query",
    "Cal 50mmHg",
    "Cal 150mmHg",
    "Cal 250mmHg",
    "Test 100mmHg",
    "Test 200mmHg",
    "Test 285mmHg",
    "Result",
    "Refill",
    "Error",
    "PCBA current test",
    "RTC debug",
    "PCBA timing diagnostic",
    "Single tank PCBA diagnostic",
    "Single tank loop",
    "Single PCBA flow",
    "PCBA pressure query",
    "PCBA write flash",
    "Sensor calibration"
};

static void reset_single_tank_loop_context(void);
static void single_tank_close_all(const TankLoopSpec *tank);
static void single_tank_start_settling(void);
static void single_tank_start_sampling(void);
static void single_tank_start_fast_refill(const TankLoopSpec *tank, uint32_t current_pressure_001mmhg);
static void single_tank_start_pulse(const TankLoopSpec *tank, SingleTankPulseMode mode);
static void single_tank_trip_protection(uint8_t tank_index, AppSingleTankProtectionReason reason);
static void single_tank_loop_task(void);
static void clear_manual_valve_overrides(void);
static uint8_t pressure_sensor_fault_active(PressureSensorIndex sensor);
static uint8_t pressure_sensor_fault_latched(PressureSensorIndex sensor);
static uint8_t any_tank_sensor_fault_active(void);
static uint8_t any_output_sensor_fault_active(void);
static uint8_t output_sensor_fault_active(uint8_t channel_index);
static uint32_t pressure_hard_limit_001mmhg(uint32_t target_001mmhg);
static uint32_t single_tank_fast_refill_threshold_001mmhg(uint32_t target_001mmhg,
                                                          uint32_t tolerance_001mmhg);
static uint8_t pressure_above_limit_active(PressureSensorIndex sensor, uint32_t limit_001mmhg);
static uint8_t any_tank_pressure_above_default_limits_active(void);
static uint8_t any_output_pressure_above_limit_active(uint32_t target_001mmhg);
static uint8_t output_pressure_above_limit_active(uint8_t channel_index, uint32_t target_001mmhg);
static uint8_t state_has_blocking_pressure_latch(AppRuntimeState state);
static void reset_pcba_result_flags(void);
static void reset_pcba_timing_report(void);
static void reset_single_tank_pcba_report(void);
static void diagnostic_delay_ms(uint32_t delay_ms);
static uint8_t single_tank_pcba_diagnostic_active(void);
static void store_pcba_timing_entry(uint8_t kind,
                                    uint8_t cmd_sent,
                                    uint8_t ok,
                                    uint8_t resp_cmd_or_byte,
                                    uint8_t resp_channel,
                                    uint8_t resp_len,
                                    uint8_t resp_data0,
                                    uint32_t elapsed_us);
static void store_single_tank_pcba_entry(uint8_t kind,
                                         uint8_t cmd_sent,
                                         uint8_t ok,
                                         uint8_t flags,
                                         const PcbaFrame *response,
                                         uint32_t current_ua_x100,
                                         uint32_t elapsed_us,
                                         uint32_t parsed_value);
static void store_single_tank_pcba_entry_deferred(uint8_t kind,
                                                  uint8_t cmd_sent,
                                                  uint8_t ok,
                                                  uint8_t flags,
                                                  const PcbaFrame *response,
                                                  uint32_t current_ua_x100,
                                                  uint32_t elapsed_us,
                                                  uint32_t parsed_value);
static void update_last_pcba_timing_raw_from_frame(const PcbaFrame *response);
static void update_last_pcba_timing_raw_byte(uint8_t response_byte, uint8_t valid);
static void run_pcba_timing_diagnostic(void);
static void run_single_tank_pcba_diagnostic(void);
static uint8_t single_tank_pcba_drive_first_tank(uint32_t target_001mmhg,
                                                 SingleTankPcbaTrendCapture *capture,
                                                 uint8_t *failure_reason);
static uint8_t single_tank_pcba_vent_zero(uint32_t *vented_pressure_001mmhg,
                                          uint8_t *failure_reason,
                                          uint8_t ignore_stop);
static SingleTankPcbaPressureStepResult single_tank_pcba_prepare_pressure_step(
    uint32_t target_001mmhg,
    uint8_t dependent_command,
    uint8_t flags,
    SingleTankPcbaTrendCapture *capture);
static uint8_t single_tank_pcba_send_ack_step(uint8_t cmd,
                                              uint32_t payload_value,
                                              uint8_t payload_len,
                                              uint8_t flags,
                                              uint32_t parsed_value);
static uint8_t single_tank_pcba_query_pressure_step(uint32_t target_001mmhg,
                                                    uint8_t flags,
                                                    const SingleTankPcbaTrendCapture *capture,
                                                    uint8_t store_trend_entry);
static uint8_t single_tank_pcba_send_calibration_trend_step(
    uint8_t flags,
    const SingleTankPcbaTrendCapture *capture);
static void pressure_test_step_single_pcba(PressureSensorIndex sensor,
                                           uint32_t target,
                                           uint8_t outlet_valve,
                                           AppRuntimeState next);
static void single_pcba_protocol_cal_step(uint32_t pressure_001mmhg, AppRuntimeState next);
static uint8_t pcba_has_any_response(const PcbaFrame *response);

static const uint8_t s_single_pcba_channel_index = 0u;
static const uint8_t s_single_pcba_uart_route = 1u;
static const uint8_t s_single_pcba_frame_channel = 0u;

static void enter_state(AppRuntimeState state)
{
    const AppRuntimeState previous_state = s_app.state;

    if (previous_state == APP_STATE_SINGLE_TANK_LOOP &&
        state != APP_STATE_SINGLE_TANK_LOOP) {
        reset_single_tank_loop_context();
    }
    if (previous_state == APP_STATE_SENSOR_CALIBRATION &&
        state != APP_STATE_SENSOR_CALIBRATION) {
        AppSensorCalibration_Exit();
        AppValves_AllClosed();
    }

    s_app.state = state;
    s_app.entered_at = HAL_GetTick();
    s_app.step_sent = 0u;
    if (previous_state == APP_STATE_PCBA_CURRENT_TEST &&
        state != APP_STATE_PCBA_CURRENT_TEST) {
        AppPower_Enable50mATestCircuit(0);
    }
    if (state == APP_STATE_ERROR) {
        s_app.running = 0u;
        clear_manual_valve_overrides();
    }
    if (state == APP_STATE_SINGLE_TANK_LOOP) {
        s_app.single_tank_phase_started_at = s_app.entered_at;
        s_app.single_tank_last_sample_at = 0u;
        s_app.single_tank_sample_accum_001mmhg = 0u;
        s_app.single_tank_sample_count = 0u;
        s_app.single_tank_last_settled_valid = 0u;
        s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_NONE;
        s_app.single_tank_last_refill_progress_at = 0u;
        s_app.single_tank_last_refill_reference_valid = 0u;
        s_app.single_tank_refill_no_rise_count = 0u;
        s_app.single_tank_protection_active = 0u;
        s_app.single_tank_protection_reason = APP_SINGLE_TANK_PROTECTION_NONE;
        s_app.single_tank_protection_tank_index = 0u;
        s_app.single_tank_protection_sensor_index = 0u;
        s_app.single_tank_protection_inlet_valve = 0u;
    }
}

static void set_all_flags(uint8_t *flags, uint8_t value)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        flags[i] = value;
    }
}

static uint8_t elapsed(uint32_t ms)
{
    return (HAL_GetTick() - s_app.entered_at) >= ms;
}

static void open_output_to_all_channels(uint8_t outlet_valve)
{
    AppValves_AllClosed();
    AppValves_Set(outlet_valve, 1u);
    AppValves_OpenMask(s_channel_valves, sizeof(s_channel_valves));
}

static void open_output_to_channel(uint8_t outlet_valve, uint8_t channel_index)
{
    AppValves_AllClosed();
    AppValves_Set(outlet_valve, 1u);
    if (channel_index < APP_PCBA_CHANNEL_COUNT) {
        AppValves_Set(s_channel_valves[channel_index], 1u);
    }
}

static void clear_manual_valve_overrides(void)
{
    s_app.manual_mode = 0u;
    s_app.manual_valve_override_mask = 0u;
    s_app.manual_valve_open_mask = 0u;
}

static void reset_pcba_result_flags(void)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        s_app.pcba_test_pressure[i] = 0u;
        s_app.pcba_online[i] = 0u;
        s_app.pcba_low_power_ok[i] = 0u;
        s_app.pcba_normal_power_ok[i] = 0u;
    }
}

static void reset_pcba_timing_report(void)
{
    s_pcba_timing_report.running = 0u;
    s_pcba_timing_report.done = 0u;
    s_pcba_timing_report.count = 0u;
    s_pcba_timing_report.final_result = 0u;
    for (uint8_t i = 0u; i < APP_PCBA_TIMING_STEP_COUNT; ++i) {
        s_pcba_timing_report.entries[i].kind = 0u;
        s_pcba_timing_report.entries[i].cmd_sent = 0u;
        s_pcba_timing_report.entries[i].ok = 0u;
        s_pcba_timing_report.entries[i].resp_cmd_or_byte = 0u;
        s_pcba_timing_report.entries[i].resp_channel = 0u;
        s_pcba_timing_report.entries[i].resp_len = 0u;
        s_pcba_timing_report.entries[i].resp_data0 = 0u;
        s_pcba_timing_report.entries[i].resp_data1 = 0u;
        s_pcba_timing_report.entries[i].resp_data2 = 0u;
        s_pcba_timing_report.entries[i].resp_data3 = 0u;
        s_pcba_timing_report.entries[i].raw_len = 0u;
        for (uint8_t raw_i = 0u; raw_i < APP_PCBA_REPORT_RAW_MAX; ++raw_i) {
            s_pcba_timing_report.entries[i].raw[raw_i] = 0u;
        }
        s_pcba_timing_report.entries[i].elapsed_us = 0u;
    }
}

static void reset_single_tank_pcba_report(void)
{
    s_single_tank_pcba_report.running = 0u;
    s_single_tank_pcba_report.done = 0u;
    s_single_tank_pcba_report.count = 0u;
    s_single_tank_pcba_report.final_result = 0u;
    s_single_tank_pcba_report.standby_current_ua_x100 = 0u;
    s_single_tank_pcba_report.work_current_ua_x100 = 0u;
    for (uint8_t i = 0u; i < APP_SINGLE_TANK_PCBA_STEP_COUNT; ++i) {
        s_single_tank_pcba_report.entries[i].kind = 0u;
        s_single_tank_pcba_report.entries[i].cmd_sent = 0u;
        s_single_tank_pcba_report.entries[i].ok = 0u;
        s_single_tank_pcba_report.entries[i].flags = 0u;
        s_single_tank_pcba_report.entries[i].resp_cmd_or_byte = 0u;
        s_single_tank_pcba_report.entries[i].resp_channel = 0u;
        s_single_tank_pcba_report.entries[i].resp_len = 0u;
        s_single_tank_pcba_report.entries[i].resp_data0 = 0u;
        s_single_tank_pcba_report.entries[i].resp_data1 = 0u;
        s_single_tank_pcba_report.entries[i].resp_data2 = 0u;
        s_single_tank_pcba_report.entries[i].resp_data3 = 0u;
        s_single_tank_pcba_report.entries[i].raw_len = 0u;
        for (uint8_t raw_i = 0u; raw_i < APP_PCBA_REPORT_RAW_MAX; ++raw_i) {
            s_single_tank_pcba_report.entries[i].raw[raw_i] = 0u;
        }
        s_single_tank_pcba_report.entries[i].current_ua_x100 = 0u;
        s_single_tank_pcba_report.entries[i].elapsed_us = 0u;
        s_single_tank_pcba_report.entries[i].parsed_value = 0u;
    }
}

static void store_pcba_timing_entry(uint8_t kind,
                                    uint8_t cmd_sent,
                                    uint8_t ok,
                                    uint8_t resp_cmd_or_byte,
                                    uint8_t resp_channel,
                                    uint8_t resp_len,
                                    uint8_t resp_data0,
                                    uint32_t elapsed_us)
{
    uint8_t index = s_pcba_timing_report.count;

    if (index >= APP_PCBA_TIMING_STEP_COUNT) {
        return;
    }
    s_pcba_timing_report.entries[index].kind = kind;
    s_pcba_timing_report.entries[index].cmd_sent = cmd_sent;
    s_pcba_timing_report.entries[index].ok = ok;
    s_pcba_timing_report.entries[index].resp_cmd_or_byte = resp_cmd_or_byte;
    s_pcba_timing_report.entries[index].resp_channel = resp_channel;
    s_pcba_timing_report.entries[index].resp_len = resp_len;
    s_pcba_timing_report.entries[index].resp_data0 = resp_data0;
    s_pcba_timing_report.entries[index].resp_data1 = 0u;
    s_pcba_timing_report.entries[index].resp_data2 = 0u;
    s_pcba_timing_report.entries[index].resp_data3 = 0u;
    s_pcba_timing_report.entries[index].raw_len = 0u;
    for (uint8_t i = 0u; i < APP_PCBA_REPORT_RAW_MAX; ++i) {
        s_pcba_timing_report.entries[index].raw[i] = 0u;
    }
    s_pcba_timing_report.entries[index].elapsed_us = elapsed_us;
    s_pcba_timing_report.count = (uint8_t)(index + 1u);
}

static void update_last_pcba_timing_raw_from_frame(const PcbaFrame *response)
{
    if (response == 0 || s_pcba_timing_report.count == 0u) {
        return;
    }

    AppPcbaTimingEntry *entry = &s_pcba_timing_report.entries[s_pcba_timing_report.count - 1u];
    entry->resp_data1 = response->len > 1u ? response->data[1] : 0u;
    entry->resp_data2 = response->len > 2u ? response->data[2] : 0u;
    entry->resp_data3 = response->len > 3u ? response->data[3] : 0u;
    entry->raw_len = response->raw_len <= APP_PCBA_REPORT_RAW_MAX ? response->raw_len : APP_PCBA_REPORT_RAW_MAX;
    for (uint8_t i = 0u; i < APP_PCBA_REPORT_RAW_MAX; ++i) {
        entry->raw[i] = i < entry->raw_len ? response->raw[i] : 0u;
    }
    AppUsbControl_Task();
}

static void update_last_pcba_timing_raw_byte(uint8_t response_byte, uint8_t valid)
{
    if (s_pcba_timing_report.count == 0u) {
        return;
    }

    AppPcbaTimingEntry *entry = &s_pcba_timing_report.entries[s_pcba_timing_report.count - 1u];
    entry->raw_len = valid != 0u ? 1u : 0u;
    for (uint8_t i = 0u; i < APP_PCBA_REPORT_RAW_MAX; ++i) {
        entry->raw[i] = i == 0u && valid != 0u ? response_byte : 0u;
    }
    AppUsbControl_Task();
}

static uint8_t pcba_timing_entry_failed(const AppPcbaTimingEntry *entry)
{
    if (entry == 0) {
        return 0u;
    }
    return (entry->ok == 0u || entry->elapsed_us > APP_PCBA_TIMING_PASS_LIMIT_US) ? 1u : 0u;
}

static uint32_t current_ua_to_x100_local(float current_ua)
{
    if (current_ua <= 0.0f) {
        return 0u;
    }
    if (current_ua >= 42949672.0f) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)((current_ua * 100.0f) + 0.5f);
}

static void diagnostic_delay_ms(uint32_t delay_ms)
{
    const uint32_t started = HAL_GetTick();

    while ((HAL_GetTick() - started) < delay_ms) {
        AppUsbControl_Task();
        HAL_Delay(1u);
    }
}

static uint8_t single_tank_pcba_diagnostic_active(void)
{
    return s_app.running != 0u &&
           s_app.state == APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC ? 1u : 0u;
}

static void store_single_tank_pcba_entry_internal(uint8_t kind,
                                                  uint8_t cmd_sent,
                                                  uint8_t ok,
                                                  uint8_t flags,
                                                  const PcbaFrame *response,
                                                  uint32_t current_ua_x100,
                                                  uint32_t elapsed_us,
                                                  uint32_t parsed_value,
                                                  uint8_t publish)
{
    uint8_t index = s_single_tank_pcba_report.count;
    AppSingleTankPcbaEntry *entry;

    if (index >= APP_SINGLE_TANK_PCBA_STEP_COUNT) {
        return;
    }

    entry = &s_single_tank_pcba_report.entries[index];
    entry->kind = kind;
    entry->cmd_sent = cmd_sent;
    entry->ok = ok;
    entry->flags = flags;
    entry->current_ua_x100 = current_ua_x100;
    entry->elapsed_us = elapsed_us;
    entry->parsed_value = parsed_value;
    entry->resp_cmd_or_byte = 0u;
    entry->resp_channel = 0u;
    entry->resp_len = 0u;
    entry->resp_data0 = 0u;
    entry->resp_data1 = 0u;
    entry->resp_data2 = 0u;
    entry->resp_data3 = 0u;
    entry->raw_len = 0u;
    for (uint8_t i = 0u; i < APP_PCBA_REPORT_RAW_MAX; ++i) {
        entry->raw[i] = 0u;
    }
    if (response != 0) {
        entry->resp_cmd_or_byte = response->cmd;
        entry->resp_channel = response->channel;
        entry->resp_len = (uint8_t)response->len;
        entry->resp_data0 = response->len > 0u ? response->data[0] : 0u;
        entry->resp_data1 = response->len > 1u ? response->data[1] : 0u;
        entry->resp_data2 = response->len > 2u ? response->data[2] : 0u;
        entry->resp_data3 = response->len > 3u ? response->data[3] : 0u;
        entry->raw_len = response->raw_len <= APP_PCBA_REPORT_RAW_MAX ? response->raw_len : APP_PCBA_REPORT_RAW_MAX;
        for (uint8_t i = 0u; i < entry->raw_len; ++i) {
            entry->raw[i] = response->raw[i];
        }
    }
    s_single_tank_pcba_report.count = (uint8_t)(index + 1u);
    if (publish != 0u) {
        AppUsbControl_PublishSingleTankPcbaReport();
        AppUsbControl_Task();
    }
}

static void store_single_tank_pcba_entry(uint8_t kind,
                                         uint8_t cmd_sent,
                                         uint8_t ok,
                                         uint8_t flags,
                                         const PcbaFrame *response,
                                         uint32_t current_ua_x100,
                                         uint32_t elapsed_us,
                                         uint32_t parsed_value)
{
    store_single_tank_pcba_entry_internal(kind, cmd_sent, ok, flags, response,
                                          current_ua_x100, elapsed_us,
                                          parsed_value, 1u);
}

static void store_single_tank_pcba_entry_deferred(uint8_t kind,
                                                  uint8_t cmd_sent,
                                                  uint8_t ok,
                                                  uint8_t flags,
                                                  const PcbaFrame *response,
                                                  uint32_t current_ua_x100,
                                                  uint32_t elapsed_us,
                                                  uint32_t parsed_value)
{
    store_single_tank_pcba_entry_internal(kind, cmd_sent, ok, flags, response,
                                          current_ua_x100, elapsed_us,
                                          parsed_value, 0u);
}

static void reset_single_tank_loop_context(void)
{
    if (s_app.single_tank_index < APP_TANK_COUNT) {
        single_tank_close_all(&s_tank_loop_specs[s_app.single_tank_index]);
    }
    s_app.single_tank_active = 0u;
    s_app.single_tank_index = 0u;
    s_app.single_tank_target_001mmhg = 0u;
    s_app.single_tank_tolerance_001mmhg = 0u;
    s_app.single_tank_phase_started_at = 0u;
    s_app.single_tank_last_sample_at = 0u;
    s_app.single_tank_sample_accum_001mmhg = 0u;
    s_app.single_tank_sample_count = 0u;
    s_app.single_tank_last_settled_pressure_001mmhg = 0u;
    s_app.single_tank_last_settled_valid = 0u;
    s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_NONE;
    s_app.single_tank_last_refill_reference_001mmhg = 0u;
    s_app.single_tank_last_refill_progress_at = 0u;
    s_app.single_tank_last_refill_reference_valid = 0u;
    s_app.single_tank_refill_no_rise_count = 0u;
    s_app.single_tank_protection_active = 0u;
    s_app.single_tank_protection_reason = APP_SINGLE_TANK_PROTECTION_NONE;
    s_app.single_tank_protection_tank_index = 0u;
    s_app.single_tank_protection_sensor_index = 0u;
    s_app.single_tank_protection_inlet_valve = 0u;
}

static void apply_manual_valve_overrides(void)
{
    if (s_app.manual_mode == 0u ||
        s_app.state == APP_STATE_ERROR ||
        AppPressure_HasAnyFaultLatched() != 0) {
        return;
    }

    for (uint8_t valve = 1u; valve <= 26u; ++valve) {
        uint32_t bit = (uint32_t)1u << (valve - 1u);
        if ((s_app.manual_valve_override_mask & bit) != 0u) {
            AppValves_Set(valve, (s_app.manual_valve_open_mask & bit) != 0u ? 1u : 0u);
        }
    }
}

static void refill_tanks(void)
{
    static const uint8_t inlet_valves[6] = {1u, 3u, 5u, 7u, 9u, 11u};

    AppValves_AllClosed();
    AppValves_OpenMask(inlet_valves, sizeof(inlet_valves));
}

static uint8_t pressure_sensor_fault_active(PressureSensorIndex sensor)
{
    const uint8_t index = (uint8_t)sensor;

    if (index >= APP_PRESSURE_SENSOR_COUNT) {
        return 1u;
    }

    if (AppPressure_IsValid(sensor)) {
        s_pressure_invalid_since[index] = 0u;
        return 0u;
    }

    if (s_pressure_invalid_since[index] == 0u) {
        s_pressure_invalid_since[index] = HAL_GetTick();
        return 0u;
    }

    return (HAL_GetTick() - s_pressure_invalid_since[index]) >= APP_PRESSURE_INVALID_ABORT_MS;
}

static uint8_t pressure_sensor_fault_latched(PressureSensorIndex sensor)
{
    return AppPressure_IsFaultLatched(sensor) ? 1u : 0u;
}

static uint8_t any_tank_sensor_fault_active(void)
{
    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        if (pressure_sensor_fault_active(s_tank_loop_specs[i].sensor) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t any_output_sensor_fault_active(void)
{
    for (uint8_t i = PRESSURE_SENSOR_CH1; i <= PRESSURE_SENSOR_CH8; ++i) {
        if (pressure_sensor_fault_active((PressureSensorIndex)i) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t output_sensor_fault_active(uint8_t channel_index)
{
    if (channel_index >= APP_PCBA_CHANNEL_COUNT) {
        return 1u;
    }
    return pressure_sensor_fault_active((PressureSensorIndex)(PRESSURE_SENSOR_CH1 + channel_index));
}

static uint32_t pressure_hard_limit_001mmhg(uint32_t target_001mmhg)
{
    uint32_t limit_001mmhg = target_001mmhg + APP_PRESSURE_HARD_LIMIT_MARGIN_001MMHG;

    if (limit_001mmhg > APP_PRESSURE_HARD_LIMIT_ABS_001MMHG) {
        limit_001mmhg = APP_PRESSURE_HARD_LIMIT_ABS_001MMHG;
    }

    return limit_001mmhg;
}

static uint32_t single_tank_fast_refill_threshold_001mmhg(uint32_t target_001mmhg,
                                                          uint32_t tolerance_001mmhg)
{
    return target_001mmhg > tolerance_001mmhg ? (target_001mmhg - tolerance_001mmhg) : 0u;
}

static uint8_t pressure_above_limit_active(PressureSensorIndex sensor, uint32_t limit_001mmhg)
{
    if (!AppPressure_IsValid(sensor)) {
        return 0u;
    }

    return AppPressure_GetSafety001mmHg(sensor) >= limit_001mmhg;
}

static uint8_t any_tank_pressure_above_default_limits_active(void)
{
    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        const TankLoopSpec *tank = &s_tank_loop_specs[i];
        if (pressure_above_limit_active(tank->sensor,
                                        pressure_hard_limit_001mmhg(tank->default_target_001mmhg)) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t any_output_pressure_above_limit_active(uint32_t target_001mmhg)
{
    const uint32_t limit_001mmhg = pressure_hard_limit_001mmhg(target_001mmhg);

    for (uint8_t i = PRESSURE_SENSOR_CH1; i <= PRESSURE_SENSOR_CH8; ++i) {
        if (pressure_above_limit_active((PressureSensorIndex)i, limit_001mmhg) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t output_pressure_above_limit_active(uint8_t channel_index, uint32_t target_001mmhg)
{
    if (channel_index >= APP_PCBA_CHANNEL_COUNT) {
        return 1u;
    }
    return pressure_above_limit_active((PressureSensorIndex)(PRESSURE_SENSOR_CH1 + channel_index),
                                       pressure_hard_limit_001mmhg(target_001mmhg));
}

static uint8_t state_has_blocking_pressure_latch(AppRuntimeState state)
{
    uint16_t required_sensor_mask = APP_PRESSURE_ALL_SENSOR_MASK;

    if (state == APP_STATE_PCBA_CURRENT_TEST ||
        state == APP_STATE_RTC_DEBUG ||
        state == APP_STATE_PCBA_TIMING_DIAGNOSTIC ||
        state == APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC ||
        state == APP_STATE_SENSOR_CALIBRATION) {
        return 0u;
    }
    if (state == APP_STATE_SINGLE_PCBA_FLOW || s_app.single_pcba_mode != 0u) {
        return 0u;
    }
    if (state == APP_STATE_INIT_TANKS ||
        state == APP_STATE_AUTO_AIRTIGHTNESS ||
        state == APP_STATE_READY ||
        state == APP_STATE_REFILL ||
        state == APP_STATE_SINGLE_TANK_LOOP) {
        required_sensor_mask = APP_PRESSURE_TANK_SENSOR_MASK;
    }
    return AppPressureScope_HasBlockingFault(AppPressure_GetFaultLatchedMask(),
                                             required_sensor_mask);
}

static void single_tank_close_all(const TankLoopSpec *tank)
{
    if (tank == 0) {
        return;
    }

    AppValves_Set(tank->inlet_valve, 0u);
    AppValves_Set(tank->outlet_valve, 0u);
    AppValves_Set(tank->relief_valve, 0u);
}

static void single_tank_start_settling(void)
{
    s_app.step_sent = SINGLE_TANK_PHASE_SETTLING;
    s_app.single_tank_phase_started_at = HAL_GetTick();
    s_app.single_tank_last_sample_at = 0u;
    s_app.single_tank_sample_accum_001mmhg = 0u;
    s_app.single_tank_sample_count = 0u;
}

static void single_tank_start_sampling(void)
{
    s_app.step_sent = SINGLE_TANK_PHASE_SAMPLING;
    s_app.single_tank_phase_started_at = HAL_GetTick();
    s_app.single_tank_last_sample_at = 0u;
    s_app.single_tank_sample_accum_001mmhg = 0u;
    s_app.single_tank_sample_count = 0u;
}

static void single_tank_start_fast_refill(const TankLoopSpec *tank, uint32_t current_pressure_001mmhg)
{
    if (tank == 0) {
        return;
    }

    single_tank_close_all(tank);
    AppValves_Set(tank->inlet_valve, 1u);
    s_app.step_sent = SINGLE_TANK_PHASE_FAST_REFILL;
    s_app.single_tank_phase_started_at = HAL_GetTick();
    s_app.single_tank_last_sample_at = 0u;
    s_app.single_tank_last_refill_reference_001mmhg = current_pressure_001mmhg;
    s_app.single_tank_last_refill_progress_at = s_app.single_tank_phase_started_at;
    s_app.single_tank_last_refill_reference_valid = 1u;
    s_app.single_tank_refill_no_rise_count = 0u;
}

static void single_tank_trip_protection(uint8_t tank_index, AppSingleTankProtectionReason reason)
{
    const TankLoopSpec *tank;

    if (tank_index >= APP_TANK_COUNT) {
        return;
    }

    tank = &s_tank_loop_specs[tank_index];
    single_tank_close_all(tank);
    s_app.step_sent = SINGLE_TANK_PHASE_PROTECTED;
    s_app.single_tank_phase_started_at = HAL_GetTick();
    s_app.single_tank_last_sample_at = 0u;
    s_app.single_tank_sample_accum_001mmhg = 0u;
    s_app.single_tank_sample_count = 0u;
    s_app.single_tank_last_refill_reference_valid = 0u;
    s_app.single_tank_refill_no_rise_count = 0u;
    s_app.single_tank_protection_active = 1u;
    s_app.single_tank_protection_reason = (uint8_t)reason;
    s_app.single_tank_protection_tank_index = tank_index;
    s_app.single_tank_protection_sensor_index = (uint8_t)tank->sensor;
    s_app.single_tank_protection_inlet_valve = tank->inlet_valve;
}

static void single_tank_start_pulse(const TankLoopSpec *tank, SingleTankPulseMode mode)
{
    if (tank == 0) {
        return;
    }

    single_tank_close_all(tank);
    if (mode == SINGLE_TANK_PULSE_REFILL) {
        AppValves_Set(tank->inlet_valve, 1u);
        s_app.step_sent = SINGLE_TANK_PHASE_PULSE_REFILL;
    } else if (mode == SINGLE_TANK_PULSE_RELIEF) {
        AppValves_Set(tank->relief_valve, 1u);
        s_app.step_sent = SINGLE_TANK_PHASE_PULSE_RELIEF;
    } else {
        s_app.step_sent = SINGLE_TANK_PHASE_IDLE;
    }
    s_app.single_tank_phase_started_at = HAL_GetTick();
}

static void single_tank_loop_task(void)
{
    const uint32_t now = HAL_GetTick();
    const uint8_t tank_index = s_app.single_tank_index;
    const TankLoopSpec *tank;
    uint32_t measured;
    uint32_t tolerance;
    uint32_t target;
    uint32_t upper;
    uint32_t lower;
    uint32_t decision_pressure;
    uint32_t reverse_guard_limit;
    uint32_t hard_limit_001mmhg;
    uint32_t fast_refill_threshold_001mmhg;
    uint32_t pressure_rise_001mmhg;
    uint32_t fast_refill_progress_delta_001mmhg;

    if (s_app.single_tank_active == 0u || tank_index >= APP_TANK_COUNT) {
        AppValves_AllClosed();
        enter_state(APP_STATE_READY);
        return;
    }

    tank = &s_tank_loop_specs[tank_index];
    target = s_app.single_tank_target_001mmhg;
    tolerance = s_app.single_tank_tolerance_001mmhg;
    if (tolerance == 0u) {
        tolerance = 3u * APP_PRESSURE_SCALE_PER_MMHG;
    }
    hard_limit_001mmhg = pressure_hard_limit_001mmhg(target);
    fast_refill_threshold_001mmhg = single_tank_fast_refill_threshold_001mmhg(target, tolerance);
    if ((now - s_app.entered_at) >= APP_SINGLE_TANK_MAX_RUNTIME_MS ||
        pressure_above_limit_active(tank->sensor, hard_limit_001mmhg) != 0u) {
        single_tank_close_all(tank);
        enter_state(APP_STATE_ERROR);
        return;
    }
    if (pressure_sensor_fault_active(tank->sensor) != 0u) {
        single_tank_trip_protection(tank_index, APP_SINGLE_TANK_PROTECTION_SENSOR_FAULT);
        return;
    }
    if (s_app.single_tank_protection_active != 0u) {
        single_tank_close_all(tank);
        s_app.step_sent = SINGLE_TANK_PHASE_PROTECTED;
        return;
    }

    switch ((SingleTankLoopPhase)s_app.step_sent) {
    case SINGLE_TANK_PHASE_IDLE:
        single_tank_close_all(tank);
        if (!AppPressure_IsValid(tank->sensor)) {
            s_app.single_tank_last_settled_valid = 0u;
            return;
        }
        s_app.single_tank_last_settled_pressure_001mmhg = AppPressure_Get001mmHg(tank->sensor);
        s_app.single_tank_last_settled_valid = 1u;
        if (s_app.single_tank_last_settled_pressure_001mmhg < fast_refill_threshold_001mmhg) {
            single_tank_start_fast_refill(tank, s_app.single_tank_last_settled_pressure_001mmhg);
        } else {
            single_tank_start_sampling();
        }
        return;

    case SINGLE_TANK_PHASE_FAST_REFILL:
        AppValves_Set(tank->inlet_valve, 1u);
        AppValves_Set(tank->outlet_valve, 0u);
        AppValves_Set(tank->relief_valve, 0u);
        if (!AppPressure_IsValid(tank->sensor)) {
            return;
        }
        if (s_app.single_tank_last_sample_at != 0u &&
            (now - s_app.single_tank_last_sample_at) < APP_SINGLE_TANK_SAMPLE_INTERVAL_MS) {
            return;
        }
        measured = AppPressure_Get001mmHg(tank->sensor);
        s_app.single_tank_last_sample_at = now;
        s_app.single_tank_last_settled_pressure_001mmhg = measured;
        s_app.single_tank_last_settled_valid = 1u;
        fast_refill_progress_delta_001mmhg =
            measured > s_app.single_tank_last_refill_reference_001mmhg ?
            (measured - s_app.single_tank_last_refill_reference_001mmhg) :
            0u;
        if (fast_refill_progress_delta_001mmhg >= APP_SINGLE_TANK_PROGRESS_MIN_RISE_001MMHG) {
            s_app.single_tank_last_refill_reference_001mmhg = measured;
            s_app.single_tank_last_refill_progress_at = now;
            s_app.single_tank_refill_no_rise_count = 0u;
        }
        if ((now - s_app.single_tank_last_refill_progress_at) >= APP_SINGLE_TANK_FAST_REFILL_NO_RISE_TIMEOUT_MS) {
            single_tank_trip_protection(tank_index, APP_SINGLE_TANK_PROTECTION_NO_PRESSURE_RISE);
            return;
        }
        if (measured >= fast_refill_threshold_001mmhg) {
            single_tank_close_all(tank);
            s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_REFILL;
            single_tank_start_settling();
        }
        return;

    case SINGLE_TANK_PHASE_PULSE_REFILL:
        if ((now - s_app.single_tank_phase_started_at) >= APP_SINGLE_TANK_REFILL_PULSE_MS) {
            single_tank_close_all(tank);
            s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_REFILL;
            single_tank_start_settling();
        }
        return;

    case SINGLE_TANK_PHASE_PULSE_RELIEF:
        if ((now - s_app.single_tank_phase_started_at) >= APP_SINGLE_TANK_RELIEF_PULSE_MS) {
            single_tank_close_all(tank);
            s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_RELIEF;
            single_tank_start_settling();
        }
        return;

    case SINGLE_TANK_PHASE_SETTLING:
        single_tank_close_all(tank);
        if ((now - s_app.single_tank_phase_started_at) >= APP_SINGLE_TANK_SETTLE_MS) {
            single_tank_start_sampling();
        }
        return;

    case SINGLE_TANK_PHASE_SAMPLING:
        single_tank_close_all(tank);
        if (!AppPressure_IsValid(tank->sensor)) {
            s_app.single_tank_sample_accum_001mmhg = 0u;
            s_app.single_tank_sample_count = 0u;
            s_app.single_tank_last_sample_at = 0u;
            s_app.single_tank_last_settled_valid = 0u;
            return;
        }
        if (s_app.single_tank_last_sample_at != 0u &&
            (now - s_app.single_tank_last_sample_at) < APP_SINGLE_TANK_SAMPLE_INTERVAL_MS) {
            return;
        }

        measured = AppPressure_Get001mmHg(tank->sensor);
        s_app.single_tank_sample_accum_001mmhg += measured;
        s_app.single_tank_sample_count++;
        s_app.single_tank_last_sample_at = now;
        if (s_app.single_tank_sample_count < APP_SINGLE_TANK_SAMPLE_COUNT) {
            return;
        }

        decision_pressure = s_app.single_tank_sample_accum_001mmhg / s_app.single_tank_sample_count;
        s_app.single_tank_last_settled_pressure_001mmhg = decision_pressure;
        s_app.single_tank_last_settled_valid = 1u;
        s_app.single_tank_sample_accum_001mmhg = 0u;
        s_app.single_tank_sample_count = 0u;
        s_app.single_tank_last_sample_at = 0u;
        s_app.step_sent = SINGLE_TANK_PHASE_IDLE;

        if (s_app.single_tank_last_pulse_mode == SINGLE_TANK_PULSE_REFILL &&
            s_app.single_tank_last_refill_reference_valid != 0u) {
            pressure_rise_001mmhg = 0u;
            if (decision_pressure > s_app.single_tank_last_refill_reference_001mmhg) {
                pressure_rise_001mmhg =
                    decision_pressure - s_app.single_tank_last_refill_reference_001mmhg;
            }
            if (pressure_rise_001mmhg == 0u) {
                if (s_app.single_tank_refill_no_rise_count < 0xFFu) {
                    s_app.single_tank_refill_no_rise_count++;
                }
            } else {
                s_app.single_tank_refill_no_rise_count = 0u;
            }
            s_app.single_tank_last_refill_reference_valid = 0u;
            if (s_app.single_tank_refill_no_rise_count >= APP_SINGLE_TANK_MAX_REFILL_NO_RISE_COUNT) {
                single_tank_trip_protection(tank_index, APP_SINGLE_TANK_PROTECTION_NO_PRESSURE_RISE);
                return;
            }
        }

        upper = target + tolerance;
        lower = target > tolerance ? (target - tolerance) : 0u;
        reverse_guard_limit = target > APP_SINGLE_TANK_REVERSE_GUARD_001MMHG ?
                              (target - APP_SINGLE_TANK_REVERSE_GUARD_001MMHG) :
                              0u;

        if (decision_pressure > upper) {
            s_app.single_tank_refill_no_rise_count = 0u;
            s_app.single_tank_last_refill_reference_valid = 0u;
            single_tank_start_pulse(tank, SINGLE_TANK_PULSE_RELIEF);
        } else if (decision_pressure < lower) {
            if (s_app.single_tank_last_pulse_mode == SINGLE_TANK_PULSE_RELIEF &&
                decision_pressure >= reverse_guard_limit) {
                single_tank_start_settling();
            } else {
                s_app.single_tank_last_refill_reference_001mmhg = decision_pressure;
                s_app.single_tank_last_refill_reference_valid = 1u;
                s_app.single_tank_refill_no_rise_count = 0u;
                single_tank_start_fast_refill(tank, decision_pressure);
            }
        } else {
            s_app.single_tank_last_pulse_mode = SINGLE_TANK_PULSE_NONE;
            s_app.single_tank_refill_no_rise_count = 0u;
            s_app.single_tank_last_refill_reference_valid = 0u;
        }
        return;

    case SINGLE_TANK_PHASE_PROTECTED:
        single_tank_close_all(tank);
        return;

    default:
        s_app.step_sent = SINGLE_TANK_PHASE_IDLE;
        single_tank_close_all(tank);
        return;
    }
}

static uint8_t standby_current_check_done(void)
{
    return elapsed(APP_PCBA_STANDBY_CURRENT_CHECK_MS) &&
           AppCurrent_CaptureAll(APP_CURRENT_MODE_STANDBY) == 0 &&
           AppCurrent_StandbyAllInRange();
}

static uint8_t work_current_measure_done(void)
{
    return elapsed(APP_PCBA_WORK_CURRENT_MEASURE_MS) &&
           AppCurrent_CaptureAll(APP_CURRENT_MODE_WORK) == 0 &&
           AppCurrent_WorkAllInRange();
}

static void pcba_current_test_task(void)
{
    static uint32_t last_capture_ms;
    const uint32_t now = HAL_GetTick();

    if (s_app.pcba_supply_5v_enabled != 0u) {
        AppPower_Enable5V();
    } else {
        AppPower_Enable45V();
    }
    AppPower_Enable50mATestCircuit(s_app.pcba_current_50ma_enabled);
    if (s_app.step_sent == 0u ||
        (now - last_capture_ms) >= APP_PCBA_CURRENT_TEST_PERIOD_MS) {
        (void)AppCurrent_CaptureAll(s_app.pcba_current_50ma_enabled != 0u ?
                                    APP_CURRENT_MODE_WORK :
                                    APP_CURRENT_MODE_STANDBY);
        last_capture_ms = now;
        s_app.step_sent = 1u;
    }
}

static uint8_t all_tanks_ready(void)
{
    if (any_tank_sensor_fault_active() != 0u) {
        return 0u;
    }

    return AppPressure_IsStable(PRESSURE_SENSOR_TANK_50, APP_PRESSURE_50_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG);
}

static uint8_t pressure_step_ready(PressureSensorIndex sensor, uint32_t target)
{
    if (pressure_sensor_fault_active(sensor) != 0u) {
        return 0u;
    }
    if (!AppPressure_IsStable(sensor, target)) {
        return 0u;
    }
    if (!AppPressure_AllChannelOutputsNear(target)) {
        return 0u;
    }
    return 1u;
}

static void capture_airtight_start(void)
{
    s_app.airtight_start[0] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_50);
    s_app.airtight_start[1] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_150);
    s_app.airtight_start[2] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_250);
    s_app.airtight_start[3] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_100);
    s_app.airtight_start[4] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_200);
    s_app.airtight_start[5] = AppPressure_Get001mmHg(PRESSURE_SENSOR_TANK_285);
}

static uint8_t airtight_drop_ok(void)
{
    const PressureSensorIndex sensors[APP_TANK_COUNT] = {
        PRESSURE_SENSOR_TANK_50,
        PRESSURE_SENSOR_TANK_150,
        PRESSURE_SENSOR_TANK_250,
        PRESSURE_SENSOR_TANK_100,
        PRESSURE_SENSOR_TANK_200,
        PRESSURE_SENSOR_TANK_285
    };

    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        uint32_t now = AppPressure_Get001mmHg(sensors[i]);
        uint32_t start = s_app.airtight_start[i];
        if (start > now && (start - now) > APP_AIRTIGHTNESS_MAX_DROP_001MMHG) {
            return 0u;
        }
    }

    return 1u;
}

static void pressure_cal_step(PressureSensorIndex sensor,
                              uint32_t target,
                              uint8_t outlet_valve,
                              AppRuntimeState next)
{
    if (s_app.step_sent == 0u) {
        open_output_to_all_channels(outlet_valve);
        s_app.step_sent = 1u;
    }
    if (pressure_sensor_fault_active(sensor) != 0u ||
        any_output_sensor_fault_active() != 0u ||
        pressure_above_limit_active(sensor, pressure_hard_limit_001mmhg(target)) != 0u ||
        any_output_pressure_above_limit_active(target) != 0u) {
        enter_state(APP_STATE_ERROR);
    } else if (pressure_step_ready(sensor, target)) {
        uint32_t real_pressure = AppPressure_Get001mmHg(sensor);
        PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
        if (AppPcbaUart_SendPressureAll(PCBA_CMD_SYNC_PRESSURE_CAL,
                                        real_pressure,
                                        responses,
                                        APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
            AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
            enter_state(next);
        } else {
            enter_state(APP_STATE_ERROR);
        }
    } else if (elapsed(APP_STAGE_SETTLE_TIMEOUT_MS)) {
        enter_state(APP_STATE_ERROR);
    }
}

static void pressure_test_step(PressureSensorIndex sensor,
                               uint32_t target,
                               uint8_t outlet_valve,
                               AppRuntimeState next)
{
    if (s_app.step_sent == 0u) {
        open_output_to_all_channels(outlet_valve);
        s_app.step_sent = 1u;
    }
    if (pressure_sensor_fault_active(sensor) != 0u ||
        any_output_sensor_fault_active() != 0u ||
        pressure_above_limit_active(sensor, pressure_hard_limit_001mmhg(target)) != 0u ||
        any_output_pressure_above_limit_active(target) != 0u) {
        enter_state(APP_STATE_ERROR);
    } else if (pressure_step_ready(sensor, target)) {
        if (AppPcbaUart_SendTestAll(s_app.pcba_test_pressure, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0) {
            enter_state(next);
        } else {
            enter_state(APP_STATE_ERROR);
        }
    } else if (elapsed(APP_STAGE_SETTLE_TIMEOUT_MS)) {
        enter_state(APP_STATE_ERROR);
    }
}

static void pressure_test_step_single_pcba(PressureSensorIndex sensor,
                                           uint32_t target,
                                           uint8_t outlet_valve,
                                           AppRuntimeState next)
{
    if (s_app.step_sent == 0u) {
        open_output_to_channel(outlet_valve, s_single_pcba_channel_index);
        s_app.step_sent = 1u;
    }
    if (pressure_sensor_fault_active(sensor) != 0u ||
        output_sensor_fault_active(s_single_pcba_channel_index) != 0u ||
        pressure_above_limit_active(sensor, pressure_hard_limit_001mmhg(target)) != 0u ||
        output_pressure_above_limit_active(s_single_pcba_channel_index, target) != 0u) {
        enter_state(APP_STATE_ERROR);
    } else if (pressure_step_ready(sensor, target)) {
        if (AppPcbaUart_SendTestRoute(s_single_pcba_uart_route,
                                      s_single_pcba_frame_channel,
                                      &s_app.pcba_test_pressure[s_single_pcba_channel_index],
                                      APP_PCBA_RESPONSE_TIMEOUT_MS) == 0) {
            enter_state(next);
        } else {
            enter_state(APP_STATE_ERROR);
        }
    } else if (elapsed(APP_STAGE_SETTLE_TIMEOUT_MS)) {
        enter_state(APP_STATE_ERROR);
    }
}

static void single_pcba_protocol_cal_step(uint32_t pressure_001mmhg, AppRuntimeState next)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    const size_t len = PcbaProtocol_BuildPressure(PCBA_CMD_SYNC_PRESSURE_CAL,
                                                  s_single_pcba_frame_channel,
                                                  pressure_001mmhg,
                                                  frame,
                                                  sizeof(frame));

    AppValves_AllClosed();
    AppPower_Enable5V();
    AppPower_Enable50mATestCircuit(1);

    if (len != 0u && AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
        enter_state(next);
    } else {
        enter_state(APP_STATE_ERROR);
    }
}

static void run_pcba_timing_diagnostic(void)
{
    uint32_t elapsed_us;
    uint32_t first_elapsed_us;
    uint32_t second_elapsed_us;
    uint8_t response_byte;
    PcbaFrame response;
    PcbaFrame first_response;
    PcbaFrame second_response;
    uint32_t pressure_value = 0u;
    uint8_t boot_frame[PCBA_FRAME_MAX_SIZE];
    size_t boot_len;
#define PCBA_TIMING_STOP_IF_FAILED() \
    do { \
        if (s_pcba_timing_stop_on_fail != 0u && \
            s_pcba_timing_report.count > 0u && \
            pcba_timing_entry_failed(&s_pcba_timing_report.entries[s_pcba_timing_report.count - 1u]) != 0u) { \
            goto finalize; \
        } \
    } while (0)

    reset_pcba_timing_report();
    s_pcba_timing_report.running = 1u;

    AppValves_AllClosed();
    AppPower_AllOff();
    diagnostic_delay_ms(1000u);
    AppPower_Enable5V();
    diagnostic_delay_ms(1000u);
    AppPower_Enable50mATestCircuit(1);
    diagnostic_delay_ms(APP_SINGLE_PCBA_CURRENT_RANGE_SETTLE_MS);
    (void)AppCurrent_CaptureAll(APP_CURRENT_MODE_WORK);

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    if (AppPcbaUart_WakeOneTimed(s_single_pcba_uart_route,
                                 PCBA_WAKE_RESPONSE_BYTE,
                                 APP_PCBA_WAKE_RESPONSE_TIMEOUT_MS,
                                 &elapsed_us,
                                 &response_byte) == 0) {
        store_pcba_timing_entry(0u, PCBA_WAKE_BYTE, 1u, response_byte, 0u, 1u, response_byte, elapsed_us);
        update_last_pcba_timing_raw_byte(response_byte, 1u);
    } else {
        store_pcba_timing_entry(0u, PCBA_WAKE_BYTE, 0u, response_byte, 0u, 1u, response_byte, elapsed_us);
        update_last_pcba_timing_raw_byte(response_byte, response_byte != 0u ? 1u : 0u);
    }
    PCBA_TIMING_STOP_IF_FAILED();

    diagnostic_delay_ms(APP_PCBA_POST_WAKE_SETTLE_MS);

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    boot_len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_POWER_ON,
                                        s_single_pcba_frame_channel,
                                        boot_frame,
                                        sizeof(boot_frame));
    first_elapsed_us = 0u;
    second_elapsed_us = 0u;
    first_response.cmd = 0u; first_response.channel = 0u; first_response.len = 0u; first_response.raw_len = 0u; first_response.data[0] = 0u;
    second_response.cmd = 0u; second_response.channel = 0u; second_response.len = 0u; second_response.raw_len = 0u; second_response.data[0] = 0u;
    if (boot_len > 0u) {
        (void)AppPcbaUart_SendRawTimed(s_single_pcba_uart_route,
                                       boot_frame,
                                       (uint16_t)boot_len,
                                       &first_response,
                                       APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                       &first_elapsed_us);
        diagnostic_delay_ms(500u);
        (void)AppPcbaUart_SendRawTimed(s_single_pcba_uart_route,
                                       boot_frame,
                                       (uint16_t)boot_len,
                                       &second_response,
                                       APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                       &second_elapsed_us);
    }
    response = pcba_has_any_response(&second_response) ? second_response : first_response;
    elapsed_us = pcba_has_any_response(&second_response) ? second_elapsed_us : first_elapsed_us;
    store_pcba_timing_entry(1u,
                            PCBA_CMD_SINGLE_POWER_ON,
                            pcba_has_any_response(&first_response) || pcba_has_any_response(&second_response),
                            response.cmd,
                            response.channel,
                            (uint8_t)response.len,
                            response.len > 0u ? response.data[0] : 0u,
                            elapsed_us);
    update_last_pcba_timing_raw_from_frame(&response);
    PCBA_TIMING_STOP_IF_FAILED();

    diagnostic_delay_ms(APP_SINGLE_PCBA_POWER_ON_DELAY_MS);

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
    if (AppPcbaUart_RequestRouteTimed(s_single_pcba_uart_route,
                                      s_single_pcba_frame_channel,
                                      PCBA_CMD_SET_TEST_MODE,
                                      &response,
                                      APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                      &elapsed_us) == 0) {
        store_pcba_timing_entry(1u, PCBA_CMD_SET_TEST_MODE, 1u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
    } else {
        store_pcba_timing_entry(1u, PCBA_CMD_SET_TEST_MODE, 0u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
    }
    update_last_pcba_timing_raw_from_frame(&response);
    PCBA_TIMING_STOP_IF_FAILED();

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
    if (AppPcbaUart_RequestRouteTimed(s_single_pcba_uart_route,
                                      s_single_pcba_frame_channel,
                                      PCBA_CMD_SINGLE_QUERY_LOW_POWER,
                                      &response,
                                      APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                      &elapsed_us) == 0) {
        store_pcba_timing_entry(1u, PCBA_CMD_SINGLE_QUERY_LOW_POWER, 1u, response.cmd, response.channel, (uint8_t)response.len, response.len > 0u ? response.data[0] : 0u, elapsed_us);
    } else {
        store_pcba_timing_entry(1u, PCBA_CMD_SINGLE_QUERY_LOW_POWER, 0u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
    }
    update_last_pcba_timing_raw_from_frame(&response);
    PCBA_TIMING_STOP_IF_FAILED();

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
    if (AppPcbaUart_RequestRouteTimed(s_single_pcba_uart_route,
                                      s_single_pcba_frame_channel,
                                      PCBA_CMD_SINGLE_QUERY_NORMAL_POWER,
                                      &response,
                                      APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                      &elapsed_us) == 0) {
        store_pcba_timing_entry(1u, PCBA_CMD_SINGLE_QUERY_NORMAL_POWER, 1u, response.cmd, response.channel, (uint8_t)response.len, response.len > 0u ? response.data[0] : 0u, elapsed_us);
    } else {
        store_pcba_timing_entry(1u, PCBA_CMD_SINGLE_QUERY_NORMAL_POWER, 0u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
    }
    update_last_pcba_timing_raw_from_frame(&response);
    PCBA_TIMING_STOP_IF_FAILED();

    for (uint8_t i = 0u; i < 5u; ++i) {
        AppPcbaUart_FlushRx(s_single_pcba_uart_route);
        pressure_value = 0u;
        response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
        if (AppPcbaUart_RequestRouteTimed(s_single_pcba_uart_route,
                                          s_single_pcba_frame_channel,
                                          PCBA_CMD_PRESSURE_TEST,
                                          &response,
                                          APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                          &elapsed_us) == 0 &&
            response.crc_ok != 0u &&
            response.cmd == PCBA_CMD_PRESSURE_TEST &&
            PcbaProtocol_GetU32Le(&response, &pressure_value)) {
            store_pcba_timing_entry(1u, PCBA_CMD_PRESSURE_TEST, 1u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
        } else {
            store_pcba_timing_entry(1u, PCBA_CMD_PRESSURE_TEST, 0u, response.cmd, response.channel, (uint8_t)response.len, response.data[0], elapsed_us);
        }
        update_last_pcba_timing_raw_from_frame(&response);
        PCBA_TIMING_STOP_IF_FAILED();
    }

finalize:
    AppPower_AllOff();
    s_pcba_timing_report.running = 0u;
    s_pcba_timing_report.done = 1u;
    s_pcba_timing_report.final_result =
        (s_pcba_timing_report.count == APP_PCBA_TIMING_STEP_COUNT) ? 1u : 0u;
    for (uint8_t i = 0u; i < s_pcba_timing_report.count; ++i) {
        if (s_pcba_timing_report.entries[i].ok == 0u ||
            s_pcba_timing_report.entries[i].elapsed_us > APP_PCBA_TIMING_PASS_LIMIT_US) {
            s_pcba_timing_report.final_result = 0u;
            break;
        }
    }
    if (s_pcba_timing_report.final_result != 0u) {
        ++s_pcba_timing_pass_count;
    } else {
        ++s_pcba_timing_fail_count;
    }
#undef PCBA_TIMING_STOP_IF_FAILED
}

static uint8_t pcba_ack_is(const PcbaFrame *response, uint8_t expected)
{
    if (expected == PCBA_ACK_YES) {
        return PcbaProtocol_IsSuccessAck(response) ? 1u : 0u;
    }
    return response != 0 && response->crc_ok != 0u &&
           PcbaProtocol_IsOneByteAck(response,
                                     s_single_pcba_frame_channel,
                                     expected) ? 1u : 0u;
}

static uint8_t pcba_has_any_response(const PcbaFrame *response)
{
    return response != 0 && response->raw_len > 0u;
}

static uint8_t pcba_version_is_0a0a(const PcbaFrame *response)
{
    return response != 0 &&
           response->crc_ok != 0u &&
           response->cmd == PCBA_CMD_SET_TEST_MODE &&
           response->len == 2u &&
           response->data[0] == 0x0Au &&
           response->data[1] == 0x0Au;
}

static uint8_t pressure_within_tolerance(uint32_t measured,
                                         uint32_t target,
                                         uint32_t tolerance)
{
    uint32_t delta = measured > target ? (measured - target) : (target - measured);
    return delta <= tolerance ? 1u : 0u;
}

static uint8_t single_tank_pcba_drive_first_tank(uint32_t target_001mmhg,
                                                 SingleTankPcbaTrendCapture *capture,
                                                 uint8_t *failure_reason)
{
    const TankLoopSpec *tank = &s_tank_loop_specs[0];
    const uint32_t started_at = HAL_GetTick();
    const uint32_t hard_limit = APP_PRESSURE_HARD_LIMIT_ABS_001MMHG;
    AppPressureTrendSamples samples = {0};
    AppPressureTrendFit empty_fit = {0};
    uint32_t progress_reference = 0u;
    uint32_t progress_at = started_at;
    uint32_t valve_closed_at = 0u;
    uint32_t trend_window_started_at = 0u;
    uint32_t last_sample_sequence = AppPressure_GetSampleSequence(tank->sensor);
    uint32_t sample_spacing_ms =
        s_app.single_tank_pcba_trend_window_ms /
        (APP_PRESSURE_TREND_MAX_SAMPLES - 1u);
    uint8_t progress_valid = 0u;
    uint8_t inlet_opened = 0u;
    uint8_t target_reached = 0u;
    uint8_t valid_sample_seen = 0u;
    uint8_t no_rise_retry_count = 0u;

    if (sample_spacing_ms == 0u) {
        sample_spacing_ms = 1u;
    }
    if (capture != 0) {
        capture->fit = empty_fit;
        capture->last_pressure_001mmhg = 0u;
    }
    if (failure_reason != 0) {
        *failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    }

    open_output_to_channel(tank->outlet_valve, s_single_pcba_channel_index);
    AppValves_Set(tank->inlet_valve, 0u);
    AppValves_Set(tank->relief_valve, 0u);

    while ((HAL_GetTick() - started_at) < APP_SINGLE_TANK_PCBA_LOOP_TIMEOUT_MS) {
        uint32_t now;
        uint32_t measured;
        uint32_t sample_sequence;

        AppUsbControl_Task();
        if (single_tank_pcba_diagnostic_active() == 0u) {
            AppValves_AllClosed();
            return 0u;
        }
        AppPressure_Task();
        now = HAL_GetTick();

        if (pressure_sensor_fault_active(tank->sensor) != 0u) {
            AppValves_AllClosed();
            if (failure_reason != 0) {
                *failure_reason = APP_PRESSURE_SETTLE_FAILURE_SENSOR_FAULT;
            }
            return 0u;
        }
        if (!AppPressure_IsValid(tank->sensor)) {
            AppValves_Set(tank->inlet_valve, 0u);
            AppValves_Set(tank->relief_valve, 0u);
            if (inlet_opened != 0u || target_reached != 0u) {
                if (failure_reason != 0) {
                    *failure_reason = APP_PRESSURE_SETTLE_FAILURE_SENSOR_INVALID;
                }
                return 0u;
            }
            diagnostic_delay_ms(10u);
            continue;
        }

        sample_sequence = AppPressure_GetSampleSequence(tank->sensor);
        if (sample_sequence == last_sample_sequence) {
            diagnostic_delay_ms(5u);
            continue;
        }
        last_sample_sequence = sample_sequence;
        measured = AppPressure_Get001mmHg(tank->sensor);
        valid_sample_seen = 1u;
        if (capture != 0) {
            capture->last_pressure_001mmhg = measured;
        }
        if (AppPressure_GetSafety001mmHg(tank->sensor) >= hard_limit) {
            AppValves_AllClosed();
            if (failure_reason != 0) {
                *failure_reason = APP_PRESSURE_SETTLE_FAILURE_OVERPRESSURE;
            }
            return 0u;
        }

        if (target_reached == 0u) {
            if (measured >= target_001mmhg) {
                AppValves_Set(tank->inlet_valve, 0u);
                target_reached = 1u;
                progress_valid = 0u;
                valve_closed_at = now;
                trend_window_started_at =
                    now + APP_SINGLE_TANK_PCBA_VALVE_CLOSE_TRANSIENT_MS;
                diagnostic_delay_ms(5u);
                continue;
            }

            if (inlet_opened == 0u) {
                AppValves_Set(tank->inlet_valve, 1u);
                inlet_opened = 1u;
            }
            if (progress_valid == 0u) {
                progress_reference = measured;
                progress_at = now;
                progress_valid = 1u;
            } else if (measured >= (progress_reference + APP_SINGLE_TANK_PROGRESS_MIN_RISE_001MMHG)) {
                progress_reference = measured;
                progress_at = now;
            } else if ((now - progress_at) >= APP_SINGLE_TANK_FAST_REFILL_NO_RISE_TIMEOUT_MS) {
                if (no_rise_retry_count < APP_SINGLE_TANK_FAST_REFILL_RETRY_COUNT) {
                    ++no_rise_retry_count;
                    AppValves_AllClosed();
                    diagnostic_delay_ms(APP_SINGLE_TANK_FAST_REFILL_RETRY_DELAY_MS);
                    if (single_tank_pcba_diagnostic_active() == 0u) {
                        return 0u;
                    }
                    open_output_to_channel(tank->outlet_valve,
                                           s_single_pcba_channel_index);
                    AppValves_Set(tank->inlet_valve, 0u);
                    AppValves_Set(tank->relief_valve, 0u);
                    progress_valid = 0u;
                    inlet_opened = 0u;
                    continue;
                }
                AppValves_AllClosed();
                if (failure_reason != 0) {
                    *failure_reason = APP_PRESSURE_SETTLE_FAILURE_NO_PRESSURE_RISE;
                }
                return 0u;
            }
            diagnostic_delay_ms(5u);
            continue;
        }

        AppValves_Set(tank->inlet_valve, 0u);
        AppValves_Set(tank->relief_valve, 0u);
        if ((now - valve_closed_at) < APP_SINGLE_TANK_PCBA_VALVE_CLOSE_TRANSIENT_MS) {
            diagnostic_delay_ms(5u);
            continue;
        }

        {
            const uint32_t trend_elapsed_ms = now - trend_window_started_at;
            const uint32_t next_sample_due_ms =
                (uint32_t)samples.count * sample_spacing_ms;

            if (samples.count < APP_PRESSURE_TREND_MAX_SAMPLES &&
                (samples.count == 0u ||
                 trend_elapsed_ms >= next_sample_due_ms ||
                 trend_elapsed_ms >= s_app.single_tank_pcba_trend_window_ms)) {
                samples.time_ms[samples.count] = now;
                samples.pressure_001mmhg[samples.count] = measured;
                ++samples.count;
            }

            if (trend_elapsed_ms >= s_app.single_tank_pcba_trend_window_ms) {
                if (capture == 0 ||
                    AppPressureTrend_FitSamples(&samples, now, &capture->fit) == 0u) {
                    if (failure_reason != 0) {
                        *failure_reason = APP_PRESSURE_SETTLE_FAILURE_TREND_SAMPLES;
                    }
                    return 0u;
                }
                if (AppPressureTrend_IsAcceptable(
                        &capture->fit,
                        s_app.single_tank_pcba_trend_max_residual_001mmhg,
                        s_app.single_tank_pcba_max_drop_rate_001mmhg_per_s) == 0u) {
                    if (failure_reason != 0) {
                        if (capture->fit.max_residual_001mmhg >
                            s_app.single_tank_pcba_trend_max_residual_001mmhg) {
                            *failure_reason = APP_PRESSURE_SETTLE_FAILURE_TREND_RESIDUAL;
                        } else if (capture->fit.slope_001mmhg_per_s > 0) {
                            *failure_reason = APP_PRESSURE_SETTLE_FAILURE_TREND_DIRECTION;
                        } else {
                            *failure_reason = APP_PRESSURE_SETTLE_FAILURE_TREND_DROP_RATE;
                        }
                    }
                    return 0u;
                }
                return 1u;
            }
        }

        diagnostic_delay_ms(5u);
    }

    AppValves_AllClosed();
    if (failure_reason != 0) {
        *failure_reason = valid_sample_seen != 0u ?
                          APP_PRESSURE_SETTLE_FAILURE_TIMEOUT :
                          APP_PRESSURE_SETTLE_FAILURE_SENSOR_INVALID;
    }
    return 0u;
}

static uint8_t single_tank_pcba_vent_zero(uint32_t *vented_pressure_001mmhg,
                                          uint8_t *failure_reason,
                                          uint8_t ignore_stop)
{
    const TankLoopSpec *tank = &s_tank_loop_specs[0];
    const AppPressureVentLimits vent_limits = {
        APP_SINGLE_TANK_PCBA_ZERO_MAX_ABS_001MMHG,
        APP_SINGLE_TANK_PCBA_MATH_SAT_RELEASE_001MMHG,
        APP_SINGLE_TANK_PCBA_VENT_MIN_OPEN_MS,
        APP_SINGLE_TANK_PCBA_VENT_ZERO_HOLD_OPEN_MS,
        APP_SINGLE_TANK_PCBA_VENT_STATIC_SETTLE_MS,
        APP_SINGLE_TANK_PCBA_ZERO_VALID_SAMPLES
    };
    AppPressureVentController vent_controller;

    if (vented_pressure_001mmhg != 0) {
        *vented_pressure_001mmhg = 0u;
    }
    if (failure_reason != 0) {
        *failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    }

    open_output_to_channel(tank->outlet_valve, s_single_pcba_channel_index);
    AppValves_Set(tank->inlet_valve, 0u);
    AppValves_Set(tank->relief_valve, 1u);
    AppPressureVent_Init(&vent_controller,
                         HAL_GetTick(),
                         AppPressure_GetSampleSequence(tank->sensor));

    for (;;) {
        uint32_t measured;
        uint32_t sample_sequence;
        uint32_t last_safe_pressure;
        uint8_t pressure_valid;
        uint8_t fault_code;
        AppPressureVentAction vent_action;

        AppUsbControl_Task();
        if (ignore_stop == 0u &&
            single_tank_pcba_diagnostic_active() == 0u) {
            AppValves_AllClosed();
            return 0u;
        }
        AppPressure_Task();

        pressure_valid = AppPressure_IsValid(tank->sensor) ? 1u : 0u;
        fault_code = AppPressure_GetFaultCode(tank->sensor);
        sample_sequence = AppPressure_GetSampleSequence(tank->sensor);
        measured = pressure_valid != 0u ? AppPressure_Get001mmHg(tank->sensor) : 0u;
        last_safe_pressure = AppPressure_GetSafety001mmHg(tank->sensor);
        vent_action = AppPressureVent_Step(
            &vent_controller,
            &vent_limits,
            HAL_GetTick(),
            sample_sequence,
            pressure_valid,
            sample_sequence != 0u ? 1u : 0u,
            fault_code,
            measured,
            last_safe_pressure);
        AppValves_Set(tank->relief_valve,
                      vent_action == APP_PRESSURE_VENT_RELIEF_OPEN ? 1u : 0u);

        if (pressure_valid == 0u &&
            fault_code == APP_PRESSURE_FAULT_MATH_SATURATION) {
            diagnostic_delay_ms(10u);
            continue;
        }
        if (pressure_sensor_fault_active(tank->sensor) != 0u) {
            AppValves_AllClosed();
            if (failure_reason != 0) {
                *failure_reason = APP_PRESSURE_SETTLE_FAILURE_SENSOR_FAULT;
            }
            return 0u;
        }
        if (pressure_valid == 0u) {
            diagnostic_delay_ms(10u);
            continue;
        }

        if (vented_pressure_001mmhg != 0) {
            *vented_pressure_001mmhg = measured;
        }
        if (AppPressure_GetSafety001mmHg(tank->sensor) >=
            APP_PRESSURE_HARD_LIMIT_ABS_001MMHG) {
            AppValves_AllClosed();
            if (failure_reason != 0) {
                *failure_reason = APP_PRESSURE_SETTLE_FAILURE_OVERPRESSURE;
            }
            return 0u;
        }

        if (vent_action == APP_PRESSURE_VENT_ZERO_CONFIRMED) {
            AppValves_AllClosed();
            return 1u;
        }
        diagnostic_delay_ms(10u);
    }
}

static uint32_t single_tank_pcba_locked_prediction(
    const SingleTankPcbaTrendCapture *capture)
{
    if (capture == 0) {
        return 0u;
    }
    if (capture->fit.valid == 0u) {
        return capture->last_pressure_001mmhg;
    }
    return AppPressureTrend_Predict(&capture->fit, HAL_GetTick());
}

static void single_tank_pcba_store_trend_entry(
    const SingleTankPcbaTrendCapture *capture,
    uint32_t locked_pressure_001mmhg,
    uint8_t ok,
    uint8_t failure_reason,
    uint8_t flags,
    uint8_t deferred)
{
    PcbaFrame trend_response = {0};
    uint32_t residual_001mmhg = 0u;
    uint32_t elapsed_us = 0u;
    uint32_t signed_slope = 0u;

    if (capture != 0) {
        residual_001mmhg = capture->fit.max_residual_001mmhg;
        elapsed_us = capture->fit.span_ms * 1000u;
        signed_slope = (uint32_t)capture->fit.slope_001mmhg_per_s;
        trend_response.cmd = capture->fit.sample_count;
    }
    trend_response.len = 4u;
    trend_response.data[0] = (uint8_t)(residual_001mmhg & 0xFFu);
    trend_response.data[1] = (uint8_t)((residual_001mmhg >> 8) & 0xFFu);
    trend_response.data[2] = (uint8_t)((residual_001mmhg >> 16) & 0xFFu);
    trend_response.data[3] = (uint8_t)((residual_001mmhg >> 24) & 0xFFu);

    if (deferred != 0u) {
        store_single_tank_pcba_entry_deferred(4u,
                                              ok != 0u ? 0u : failure_reason,
                                              ok,
                                              flags,
                                              &trend_response,
                                              signed_slope,
                                              elapsed_us,
                                              locked_pressure_001mmhg);
    } else {
        store_single_tank_pcba_entry(4u,
                                     ok != 0u ? 0u : failure_reason,
                                     ok,
                                     flags,
                                     &trend_response,
                                     signed_slope,
                                     elapsed_us,
                                     locked_pressure_001mmhg);
    }
}

static SingleTankPcbaPressureStepResult single_tank_pcba_prepare_pressure_step(
    uint32_t target_001mmhg,
    uint8_t dependent_command,
    uint8_t flags,
    SingleTankPcbaTrendCapture *capture)
{
    uint8_t failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    uint32_t locked_pressure_001mmhg;

    if (single_tank_pcba_drive_first_tank(target_001mmhg,
                                          capture,
                                          &failure_reason) != 0u) {
        return SINGLE_TANK_PCBA_PRESSURE_READY;
    }

    if (single_tank_pcba_diagnostic_active() == 0u) {
        return SINGLE_TANK_PCBA_PRESSURE_ABORT;
    }

    locked_pressure_001mmhg = single_tank_pcba_locked_prediction(capture);
    single_tank_pcba_store_trend_entry(capture,
                                       locked_pressure_001mmhg,
                                       0u,
                                       failure_reason,
                                       flags,
                                       0u);
    if (s_single_tank_pcba_continue_on_fail == 0u ||
        AppPressureSettle_FailureCanContinue(failure_reason) == 0u) {
        return SINGLE_TANK_PCBA_PRESSURE_ABORT;
    }

    store_single_tank_pcba_entry(3u,
                                 dependent_command,
                                 0u,
                                 flags,
                                 0,
                                 0u,
                                 0u,
                                 target_001mmhg);
    return SINGLE_TANK_PCBA_PRESSURE_SKIPPED;
}

static uint8_t single_tank_pcba_send_ack_step(uint8_t cmd,
                                              uint32_t payload_value,
                                              uint8_t payload_len,
                                              uint8_t flags,
                                              uint32_t parsed_value)
{
    PcbaFrame response;
    uint32_t elapsed_us = 0u;
    uint8_t data[4];
    int rc;

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.raw_len = 0u; response.data[0] = 0u;
    if (payload_len == 4u) {
        rc = AppPcbaUart_SendPressureRouteTimed(s_single_pcba_uart_route,
                                                s_single_pcba_frame_channel,
                                                cmd,
                                                payload_value,
                                                &response,
                                                APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                                &elapsed_us);
    } else if (payload_len == 1u) {
        data[0] = (uint8_t)(payload_value & 0xFFu);
        rc = AppPcbaUart_SendDataRouteTimed(s_single_pcba_uart_route,
                                            s_single_pcba_frame_channel,
                                            cmd,
                                            data,
                                            1u,
                                            &response,
                                            APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                            &elapsed_us);
    } else {
        rc = AppPcbaUart_SendDataRouteTimed(s_single_pcba_uart_route,
                                            s_single_pcba_frame_channel,
                                            cmd,
                                            0,
                                            0u,
                                            &response,
                                            APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                            &elapsed_us);
    }

    store_single_tank_pcba_entry(1u,
                                 cmd,
                                 (rc == 0 && pcba_ack_is(&response, PCBA_ACK_YES)) ? 1u : 0u,
                                 flags,
                                 &response,
                                 0u,
                                 elapsed_us,
                                 parsed_value);
    return s_single_tank_pcba_report.entries[s_single_tank_pcba_report.count - 1u].ok;
}

static uint8_t single_tank_pcba_send_calibration_trend_step(
    uint8_t flags,
    const SingleTankPcbaTrendCapture *capture)
{
    PcbaFrame response;
    uint32_t elapsed_us = 0u;
    uint32_t locked_pressure_001mmhg;
    uint8_t ok;
    int rc;

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    locked_pressure_001mmhg = single_tank_pcba_locked_prediction(capture);
    response.cmd = 0u;
    response.channel = 0u;
    response.len = 0u;
    response.raw_len = 0u;
    response.data[0] = 0u;
    rc = AppPcbaUart_SendPressureRouteTimed(s_single_pcba_uart_route,
                                            s_single_pcba_frame_channel,
                                            PCBA_CMD_SYNC_PRESSURE_CAL,
                                            locked_pressure_001mmhg,
                                            &response,
                                            APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                            &elapsed_us);
    ok = (rc == 0 && pcba_ack_is(&response, PCBA_ACK_YES)) ? 1u : 0u;

    single_tank_pcba_store_trend_entry(capture,
                                       locked_pressure_001mmhg,
                                       1u,
                                       APP_PRESSURE_SETTLE_FAILURE_NONE,
                                       flags,
                                       1u);
    store_single_tank_pcba_entry_deferred(1u,
                                          PCBA_CMD_SYNC_PRESSURE_CAL,
                                          ok,
                                          flags,
                                          &response,
                                          0u,
                                          elapsed_us,
                                          locked_pressure_001mmhg);
    AppUsbControl_PublishSingleTankPcbaReport();
    AppUsbControl_Task();
    return ok;
}

static uint8_t single_tank_pcba_query_pressure_step(uint32_t target_001mmhg,
                                                    uint8_t flags,
                                                    const SingleTankPcbaTrendCapture *capture,
                                                    uint8_t store_trend_entry)
{
    PcbaFrame response;
    uint32_t elapsed_us = 0u;
    uint32_t pressure_001mmhg = UINT32_MAX;
    uint32_t locked_pressure_001mmhg;
    const uint32_t tolerance = s_app.pressure_tolerance_001mmhg != 0u ?
                               s_app.pressure_tolerance_001mmhg :
                               (3u * APP_PRESSURE_SCALE_PER_MMHG);
    int rc;
    uint8_t ok = 0u;

    (void)target_001mmhg;
    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    locked_pressure_001mmhg = single_tank_pcba_locked_prediction(capture);
    rc = AppPcbaUart_RequestRouteTimed(s_single_pcba_uart_route,
                                       s_single_pcba_frame_channel,
                                       PCBA_CMD_PRESSURE_TEST,
                                       &response,
                                       APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                       &elapsed_us);
    if (rc == 0 &&
        response.crc_ok != 0u &&
        response.cmd == PCBA_CMD_PRESSURE_TEST &&
        PcbaProtocol_GetPressure001mmHg(&response,
                                        &pressure_001mmhg) &&
        pressure_within_tolerance(pressure_001mmhg,
                                  locked_pressure_001mmhg,
                                  tolerance) != 0u) {
        ok = 1u;
    }

    if (store_trend_entry != 0u) {
        single_tank_pcba_store_trend_entry(capture,
                                           locked_pressure_001mmhg,
                                           1u,
                                           APP_PRESSURE_SETTLE_FAILURE_NONE,
                                           flags,
                                           1u);
    }
    store_single_tank_pcba_entry_deferred(1u,
                                          PCBA_CMD_PRESSURE_TEST,
                                          ok,
                                          flags,
                                          &response,
                                          locked_pressure_001mmhg,
                                          elapsed_us,
                                          pressure_001mmhg);
    AppUsbControl_PublishSingleTankPcbaReport();
    AppUsbControl_Task();
    return ok;
}

static void run_single_tank_pcba_diagnostic(void)
{
    uint32_t elapsed_us;
    uint32_t first_elapsed_us;
    uint32_t second_elapsed_us;
    PcbaFrame response;
    PcbaFrame first_response;
    PcbaFrame second_response;
    uint8_t boot_frame[PCBA_FRAME_MAX_SIZE];
    size_t boot_len;
    uint32_t settled_pressure;
    SingleTankPcbaTrendCapture trend_capture;
    uint8_t vent_failure_reason;
    uint8_t calibration_success_count = 0u;
    uint8_t calibration_sequence_valid = 1u;
    SingleTankPcbaPressureStepResult pressure_step_result;
    const uint8_t flags_standby = SINGLE_TANK_PCBA_FLAG_5V | SINGLE_TANK_PCBA_FLAG_CURRENT;
    const uint8_t flags_work = SINGLE_TANK_PCBA_FLAG_5V | SINGLE_TANK_PCBA_FLAG_50MA | SINGLE_TANK_PCBA_FLAG_CURRENT;
    const uint8_t flags_serial = SINGLE_TANK_PCBA_FLAG_5V | SINGLE_TANK_PCBA_FLAG_50MA;
    const uint8_t flags_low_serial = SINGLE_TANK_PCBA_FLAG_45V | SINGLE_TANK_PCBA_FLAG_50MA;

    reset_single_tank_pcba_report();
    s_single_tank_pcba_report.running = 1u;
#define SINGLE_TANK_PCBA_ABORT_IF_STOPPED() \
    do { \
        if (single_tank_pcba_diagnostic_active() == 0u) { \
            goto cancelled; \
        } \
    } while (0)
#define SINGLE_TANK_PCBA_STOP_IF_FAILED() \
    do { \
        SINGLE_TANK_PCBA_ABORT_IF_STOPPED(); \
        if (s_single_tank_pcba_continue_on_fail == 0u && \
            s_single_tank_pcba_report.count > 0u && \
            s_single_tank_pcba_report.entries[s_single_tank_pcba_report.count - 1u].ok == 0u) { \
            goto finalize; \
        } \
    } while (0)

    AppValves_AllClosed();
    AppPower_AllOff();
    AppPower_Enable50mATestCircuit(0);
    diagnostic_delay_ms(1000u);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
    AppPower_Enable5V();
    diagnostic_delay_ms(1000u);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
    (void)AppCurrent_CaptureAll(APP_CURRENT_MODE_STANDBY);
    s_single_tank_pcba_report.standby_current_ua_x100 =
        current_ua_to_x100_local(AppCurrent_GetStandbyUa(1u));
    store_single_tank_pcba_entry(2u,
                                 0u,
                                 AppCurrent_IsStandbyValid(1u),
                                 flags_standby,
                                 0,
                                 s_single_tank_pcba_report.standby_current_ua_x100,
                                 0u,
                                 0u);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    AppPower_Enable50mATestCircuit(1);
    diagnostic_delay_ms(APP_SINGLE_PCBA_CURRENT_RANGE_SETTLE_MS);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    {
        uint8_t wake_response = 0u;
        uint32_t wake_elapsed_us = 0u;

        (void)AppPcbaUart_WakeOneTimed(s_single_pcba_uart_route,
                                       PCBA_WAKE_RESPONSE_BYTE,
                                       APP_PCBA_WAKE_RESPONSE_TIMEOUT_MS,
                                       &wake_elapsed_us,
                                       &wake_response);
    }
    diagnostic_delay_ms(APP_PCBA_POST_WAKE_SETTLE_MS);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
    AppPcbaUart_FlushRx(s_single_pcba_uart_route);

    boot_len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_POWER_ON,
                                        s_single_pcba_frame_channel,
                                        boot_frame,
                                        sizeof(boot_frame));
    first_elapsed_us = 0u;
    second_elapsed_us = 0u;
    first_response.cmd = 0u; first_response.channel = 0u; first_response.len = 0u; first_response.raw_len = 0u; first_response.data[0] = 0u;
    second_response.cmd = 0u; second_response.channel = 0u; second_response.len = 0u; second_response.raw_len = 0u; second_response.data[0] = 0u;
    if (boot_len > 0u) {
        (void)AppPcbaUart_SendRawTimed(s_single_pcba_uart_route,
                                       boot_frame,
                                       (uint16_t)boot_len,
                                       &first_response,
                                       APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                       &first_elapsed_us);
        diagnostic_delay_ms(APP_SINGLE_PCBA_BOOT_RETRY_MS);
        SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
        (void)AppPcbaUart_SendRawTimed(s_single_pcba_uart_route,
                                       boot_frame,
                                       (uint16_t)boot_len,
                                       &second_response,
                                       APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                       &second_elapsed_us);
    }
    if (pcba_has_any_response(&second_response) != 0u) {
        response = second_response;
        elapsed_us = second_elapsed_us;
    } else {
        response = first_response;
        elapsed_us = first_elapsed_us;
    }
    store_single_tank_pcba_entry(1u,
                                 PCBA_CMD_SINGLE_POWER_ON,
                                 pcba_has_any_response(&first_response) || pcba_has_any_response(&second_response),
                                 flags_serial,
                                 &response,
                                 0u,
                                 elapsed_us,
                                 response.raw_len);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    (void)AppCurrent_CaptureAll(APP_CURRENT_MODE_WORK);
    s_single_tank_pcba_report.work_current_ua_x100 =
        current_ua_to_x100_local(AppCurrent_GetWorkUa(1u));
    store_single_tank_pcba_entry(2u,
                                 0u,
                                 AppCurrent_IsWorkValid(1u),
                                 flags_work,
                                 0,
                                 s_single_tank_pcba_report.work_current_ua_x100,
                                 0u,
                                 0u);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
    (void)AppPcbaUart_SendDataRouteTimed(s_single_pcba_uart_route,
                                         s_single_pcba_frame_channel,
                                         PCBA_CMD_SET_TEST_MODE,
                                         0,
                                         0u,
                                         &response,
                                         APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                         &elapsed_us);
    store_single_tank_pcba_entry(1u,
                                 PCBA_CMD_SET_TEST_MODE,
                                 pcba_version_is_0a0a(&response),
                                 flags_serial,
                                 &response,
                                 0u,
                                 elapsed_us,
                                 response.len >= 2u ? ((uint32_t)response.data[0] | ((uint32_t)response.data[1] << 8)) : 0u);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    AppPower_Enable45V();
    diagnostic_delay_ms(1000u);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
    AppPcbaUart_FlushRx(s_single_pcba_uart_route);
    response.cmd = 0u; response.channel = 0u; response.len = 0u; response.data[0] = 0u;
    (void)AppPcbaUart_SendDataRouteTimed(s_single_pcba_uart_route,
                                         s_single_pcba_frame_channel,
                                         PCBA_CMD_SINGLE_QUERY_LOW_POWER,
                                         0,
                                         0u,
                                         &response,
                                         APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS,
                                         &elapsed_us);
    store_single_tank_pcba_entry(1u,
                                 PCBA_CMD_SINGLE_QUERY_LOW_POWER,
                                 pcba_ack_is(&response, PCBA_SINGLE_LOW_POWER_ACTIVE),
                                 flags_low_serial,
                                 &response,
                                 0u,
                                 elapsed_us,
                                 response.len > 0u ? response.data[0] : 0u);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    AppPower_Enable5V();
    diagnostic_delay_ms(500u);
    SINGLE_TANK_PCBA_ABORT_IF_STOPPED();

    vent_failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    if (single_tank_pcba_vent_zero(&settled_pressure,
                                   &vent_failure_reason,
                                   0u) == 0u) {
        SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
        store_single_tank_pcba_entry(5u,
                                     vent_failure_reason,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     settled_pressure);
        goto finalize;
    }
    (void)single_tank_pcba_send_ack_step(PCBA_CMD_SINGLE_RECORD_ZERO_AD,
                                         0u,
                                         0u,
                                         flags_serial,
                                         settled_pressure);
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_50_MMHG,
                                                                  PCBA_CMD_SYNC_PRESSURE_CAL,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        if (single_tank_pcba_send_calibration_trend_step(flags_serial,
                                                         &trend_capture) != 0u) {
            ++calibration_success_count;
        } else {
            calibration_sequence_valid = 0u;
        }
        SINGLE_TANK_PCBA_STOP_IF_FAILED();
        (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_50_MMHG,
                                                   flags_serial,
                                                   &trend_capture,
                                                   0u);
        SINGLE_TANK_PCBA_STOP_IF_FAILED();
    } else if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_SKIPPED) {
        calibration_sequence_valid = 0u;
        store_single_tank_pcba_entry(3u,
                                     PCBA_CMD_PRESSURE_TEST,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     APP_PRESSURE_50_MMHG);
    }

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_150_MMHG,
                                                                  PCBA_CMD_SYNC_PRESSURE_CAL,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        if (calibration_sequence_valid != 0u) {
            if (single_tank_pcba_send_calibration_trend_step(flags_serial,
                                                             &trend_capture) != 0u) {
                ++calibration_success_count;
            } else {
                calibration_sequence_valid = 0u;
            }
            SINGLE_TANK_PCBA_STOP_IF_FAILED();
            (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_150_MMHG,
                                                       flags_serial,
                                                       &trend_capture,
                                                       0u);
            SINGLE_TANK_PCBA_STOP_IF_FAILED();
        } else {
            single_tank_pcba_store_trend_entry(
                &trend_capture,
                single_tank_pcba_locked_prediction(&trend_capture),
                1u,
                APP_PRESSURE_SETTLE_FAILURE_NONE,
                flags_serial,
                0u);
            store_single_tank_pcba_entry(3u,
                                         PCBA_CMD_SYNC_PRESSURE_CAL,
                                         0u,
                                         flags_serial,
                                         0,
                                         0u,
                                         0u,
                                         APP_PRESSURE_150_MMHG);
            store_single_tank_pcba_entry(3u,
                                         PCBA_CMD_PRESSURE_TEST,
                                         0u,
                                         flags_serial,
                                         0,
                                         0u,
                                         0u,
                                         APP_PRESSURE_150_MMHG);
        }
    } else if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_SKIPPED) {
        calibration_sequence_valid = 0u;
        store_single_tank_pcba_entry(3u,
                                     PCBA_CMD_PRESSURE_TEST,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     APP_PRESSURE_150_MMHG);
    }

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_250_MMHG,
                                                                  PCBA_CMD_SYNC_PRESSURE_CAL,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        if (calibration_sequence_valid != 0u) {
            if (single_tank_pcba_send_calibration_trend_step(flags_serial,
                                                             &trend_capture) != 0u) {
                ++calibration_success_count;
            } else {
                calibration_sequence_valid = 0u;
            }
            SINGLE_TANK_PCBA_STOP_IF_FAILED();
            (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_250_MMHG,
                                                       flags_serial,
                                                       &trend_capture,
                                                       0u);
            SINGLE_TANK_PCBA_STOP_IF_FAILED();
        } else {
            single_tank_pcba_store_trend_entry(
                &trend_capture,
                single_tank_pcba_locked_prediction(&trend_capture),
                1u,
                APP_PRESSURE_SETTLE_FAILURE_NONE,
                flags_serial,
                0u);
            store_single_tank_pcba_entry(3u,
                                         PCBA_CMD_SYNC_PRESSURE_CAL,
                                         0u,
                                         flags_serial,
                                         0,
                                         0u,
                                         0u,
                                         APP_PRESSURE_250_MMHG);
            store_single_tank_pcba_entry(3u,
                                         PCBA_CMD_PRESSURE_TEST,
                                         0u,
                                         flags_serial,
                                         0,
                                         0u,
                                         0u,
                                         APP_PRESSURE_250_MMHG);
        }
    } else if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_SKIPPED) {
        calibration_sequence_valid = 0u;
        store_single_tank_pcba_entry(3u,
                                     PCBA_CMD_PRESSURE_TEST,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     APP_PRESSURE_250_MMHG);
    }

    if (calibration_sequence_valid != 0u &&
        calibration_success_count == 3u) {
        (void)single_tank_pcba_send_ack_step(PCBA_CMD_WRITE_FLASH,
                                             0u,
                                             0u,
                                             flags_serial,
                                             calibration_success_count);
    } else {
        store_single_tank_pcba_entry(3u,
                                     PCBA_CMD_WRITE_FLASH,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     calibration_success_count);
    }
    SINGLE_TANK_PCBA_STOP_IF_FAILED();

    vent_failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    if (single_tank_pcba_vent_zero(&settled_pressure,
                                   &vent_failure_reason,
                                   0u) == 0u) {
        SINGLE_TANK_PCBA_ABORT_IF_STOPPED();
        store_single_tank_pcba_entry(5u,
                                     vent_failure_reason,
                                     0u,
                                     flags_serial,
                                     0,
                                     0u,
                                     0u,
                                     settled_pressure);
        goto finalize;
    }

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_100_MMHG,
                                                                  PCBA_CMD_PRESSURE_TEST,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_100_MMHG,
                                                   flags_serial,
                                                   &trend_capture,
                                                   1u);
        SINGLE_TANK_PCBA_STOP_IF_FAILED();
    }

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_200_MMHG,
                                                                  PCBA_CMD_PRESSURE_TEST,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_200_MMHG,
                                                   flags_serial,
                                                   &trend_capture,
                                                   1u);
        SINGLE_TANK_PCBA_STOP_IF_FAILED();
    }

    pressure_step_result = single_tank_pcba_prepare_pressure_step(APP_PRESSURE_285_MMHG,
                                                                  PCBA_CMD_PRESSURE_TEST,
                                                                  flags_serial,
                                                                  &trend_capture);
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_ABORT) {
        goto finalize;
    }
    if (pressure_step_result == SINGLE_TANK_PCBA_PRESSURE_READY) {
        (void)single_tank_pcba_query_pressure_step(APP_PRESSURE_285_MMHG,
                                                   flags_serial,
                                                   &trend_capture,
                                                   1u);
        SINGLE_TANK_PCBA_STOP_IF_FAILED();
    }

    (void)single_tank_pcba_send_ack_step(0x21u,
                                         0u,
                                         0u,
                                         flags_serial,
                                         0u);

finalize:
    diagnostic_delay_ms(500u);
    AppPower_Enable50mATestCircuit(0);
    AppPower_AllOff();
    vent_failure_reason = APP_PRESSURE_SETTLE_FAILURE_NONE;
    (void)single_tank_pcba_vent_zero(&settled_pressure,
                                     &vent_failure_reason,
                                     1u);
    AppValves_AllClosed();
    s_single_tank_pcba_report.running = 0u;
    s_single_tank_pcba_report.done = 1u;
    s_single_tank_pcba_report.final_result =
        (s_single_tank_pcba_report.count == APP_SINGLE_TANK_PCBA_STEP_COUNT) ? 1u : 0u;
    for (uint8_t i = 0u; i < s_single_tank_pcba_report.count; ++i) {
        if (s_single_tank_pcba_report.entries[i].ok == 0u) {
            s_single_tank_pcba_report.final_result = 0u;
            break;
        }
    }
    AppUsbControl_PublishSingleTankPcbaReport();
    s_app.running = 0u;
    if (s_app.state == APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC) {
        enter_state(APP_STATE_READY);
    }
#undef SINGLE_TANK_PCBA_ABORT_IF_STOPPED
#undef SINGLE_TANK_PCBA_STOP_IF_FAILED
    return;

cancelled:
    goto finalize;
}

void AppStateMachine_Init(AppBootMode mode)
{
    reset_pcba_result_flags();
    s_app.running = 0u;
    s_app.paused = 0u;
    s_app.pcba_current_50ma_enabled = 0u;
    s_app.pcba_supply_5v_enabled = 1u;
    s_app.single_pcba_mode = 0u;
    s_pcba_timing_stop_on_fail = 0u;
    s_pcba_timing_requested = 0u;
    s_single_tank_pcba_requested = 0u;
    s_single_tank_pcba_continue_on_fail = 0u;
    reset_pcba_timing_report();
    reset_single_tank_pcba_report();
    clear_manual_valve_overrides();
    reset_single_tank_loop_context();
    s_app.pressure_tolerance_001mmhg = 3u * APP_PRESSURE_SCALE_PER_MMHG;
    s_app.single_tank_pcba_trend_max_residual_001mmhg =
        APP_SINGLE_TANK_PCBA_TREND_MAX_RESIDUAL_DEFAULT_001MMHG;
    s_app.single_tank_pcba_trend_window_ms =
        APP_SINGLE_TANK_PCBA_TREND_WINDOW_DEFAULT_MS;
    s_app.single_tank_pcba_max_drop_rate_001mmhg_per_s =
        APP_SINGLE_TANK_PCBA_MAX_DROP_RATE_DEFAULT_001MMHG_PER_S;
    for (uint8_t i = 0u; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        s_pressure_invalid_since[i] = 0u;
    }

    AppPower_AllOff();
    AppValves_AllClosed();
    AppCurrent_Init();
    AppPressureCalibration_Init();
    AppPressure_Init();
    AppSensorCalibration_Init();
    AppPcbaUart_Init();

    if (mode == APP_MODE_USB_MSC) {
        BoardPins_ConfigSpiFlashProgramming();
        (void)UsbMscApp_Start();
        enter_state(APP_STATE_USB_MSC);
    } else {
        BoardPins_ConfigSpi3Float();
        enter_state(APP_STATE_READY);
    }
}

void AppStateMachine_Task(void)
{
    AppPressure_Task();

    if (s_app.state != APP_STATE_USB_MSC &&
        s_app.state != APP_STATE_ERROR &&
        s_app.state != APP_STATE_SINGLE_TANK_LOOP &&
        s_app.state != APP_STATE_SENSOR_CALIBRATION &&
        state_has_blocking_pressure_latch(s_app.state) != 0u) {
        enter_state(APP_STATE_ERROR);
    }

    if (s_app.paused != 0u && s_app.state != APP_STATE_USB_MSC) {
        return;
    }

    switch (s_app.state) {
    case APP_STATE_USB_MSC:
        UsbMscApp_Task();
        break;

    case APP_STATE_INIT_TANKS:
        if (any_tank_sensor_fault_active() != 0u ||
            any_tank_pressure_above_default_limits_active() != 0u ||
            elapsed(APP_INIT_REFILL_MAX_RUNTIME_MS)) {
            enter_state(APP_STATE_ERROR);
            break;
        }
        refill_tanks();
        if (all_tanks_ready()) {
            enter_state(APP_STATE_AUTO_AIRTIGHTNESS);
        }
        break;

    case APP_STATE_AUTO_AIRTIGHTNESS:
        if (any_tank_sensor_fault_active() != 0u) {
            enter_state(APP_STATE_ERROR);
            break;
        }
        AppValves_AllClosed();
        if (s_app.step_sent == 0u) {
            capture_airtight_start();
            s_app.step_sent = 1u;
        }
        if (elapsed(APP_AIRTIGHTNESS_HOLD_TIME_MS)) {
            enter_state(airtight_drop_ok() ? APP_STATE_READY : APP_STATE_ERROR);
        }
        break;

    case APP_STATE_READY:
        AppValves_AllClosed();
        if (AppKeys_PressSwitchActive()) {
            enter_state(APP_STATE_PCBA_POWER_ON);
        }
        break;

    case APP_STATE_PCBA_POWER_ON:
        AppPower_Enable5V();
        if (elapsed(1000u)) {
            AppPower_Enable50mATestCircuit(0);
            enter_state(APP_STATE_PCBA_STANDBY_CURRENT_CHECK);
        }
        break;

    case APP_STATE_PCBA_STANDBY_CURRENT_CHECK:
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(0);
        if (standby_current_check_done()) {
            enter_state(APP_STATE_PCBA_WAKE);
        }
        break;

    case APP_STATE_PCBA_WAKE: {
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (s_app.single_pcba_mode != 0u) {
            uint8_t wake = PCBA_WAKE_BYTE;
            if (AppPcbaUart_Send(s_single_pcba_uart_route, &wake, 1u) == 0) {
                s_app.pcba_online[s_single_pcba_channel_index] = 1u;
                enter_state(APP_STATE_PCBA_WORK_CURRENT_MEASURE);
            } else {
                enter_state(APP_STATE_ERROR);
            }
        } else {
            if (AppPcbaUart_WakeAll(PCBA_WAKE_RESPONSE_BYTE, APP_PCBA_WAKE_RESPONSE_TIMEOUT_MS) == 0) {
                set_all_flags(s_app.pcba_online, 1u);
                enter_state(APP_STATE_PCBA_WORK_CURRENT_MEASURE);
            } else {
                enter_state(APP_STATE_ERROR);
            }
        }
        break;
    }

    case APP_STATE_PCBA_WORK_CURRENT_MEASURE:
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (work_current_measure_done()) {
            enter_state(APP_STATE_PCBA_SET_TEST_MODE);
        }
        break;

    case APP_STATE_PCBA_SET_TEST_MODE:
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (s_app.single_pcba_mode != 0u) {
            if (s_app.step_sent == 0u && elapsed(APP_PCBA_POST_WAKE_SETTLE_MS)) {
                uint8_t frame[PCBA_FRAME_MAX_SIZE];
                size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_POWER_ON,
                                                      s_single_pcba_frame_channel,
                                                      frame,
                                                      sizeof(frame));
                if (len != 0u &&
                    AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
                    s_app.step_sent = 1u;
                    s_app.entered_at = HAL_GetTick();
                } else {
                    enter_state(APP_STATE_ERROR);
                }
            } else if (elapsed(APP_SINGLE_PCBA_POWER_ON_DELAY_MS)) {
                enter_state(APP_STATE_LOW_POWER_QUERY);
            }
        } else if (s_app.step_sent == 0u) {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_SendCommandAll(PCBA_CMD_SET_TEST_MODE, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
                s_app.step_sent = 1u;
            } else {
                enter_state(APP_STATE_ERROR);
            }
        } else if (s_app.step_sent == 1u) {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_SendCommandAll(PCBA_CMD_POWER_ON, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
                s_app.step_sent = 2u;
                s_app.entered_at = HAL_GetTick();
            } else {
                enter_state(APP_STATE_ERROR);
            }
        } else if (elapsed(APP_PCBA_POWER_ON_DELAY_MS)) {
            enter_state(APP_STATE_PCBA_ZERO);
        }
        break;

    case APP_STATE_PCBA_ZERO:
        AppValves_AllClosed();
        {
            if (s_app.single_pcba_mode != 0u) {
                uint8_t frame[PCBA_FRAME_MAX_SIZE];
                size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_RECORD_ZERO_AD,
                                                      s_single_pcba_frame_channel,
                                                      frame,
                                                      sizeof(frame));
                if (len != 0u &&
                    AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
                    enter_state(APP_STATE_PCBA_PRESSURE_QUERY);
                } else {
                    enter_state(APP_STATE_ERROR);
                }
            } else {
                PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
                if (AppPcbaUart_SendCommandAll(PCBA_CMD_RECORD_ZERO_AD, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                    AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
                    enter_state(APP_STATE_SWITCH_45V);
                } else {
                    enter_state(APP_STATE_ERROR);
                }
            }
        }
        break;

    case APP_STATE_SWITCH_45V:
        AppPower_Enable45V();
        if (elapsed(APP_PCBA_POWER_SWITCH_DELAY_MS)) {
            enter_state(APP_STATE_LOW_POWER_QUERY);
        }
        break;

    case APP_STATE_LOW_POWER_QUERY:
        {
            if (s_app.single_pcba_mode != 0u) {
                AppPower_Enable5V();
                AppPower_Enable50mATestCircuit(1);
                {
                    uint8_t frame[PCBA_FRAME_MAX_SIZE];
                    size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_QUERY_LOW_POWER,
                                                          s_single_pcba_frame_channel,
                                                          frame,
                                                          sizeof(frame));
                    if (len != 0u &&
                        AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
                    enter_state(APP_STATE_NORMAL_POWER_QUERY);
                    } else {
                        enter_state(APP_STATE_ERROR);
                    }
                }
            } else {
                PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
                if (AppPcbaUart_RequestAll(PCBA_CMD_QUERY_LOW_POWER_STATE, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                    AppPcbaUart_CheckOneByteAckAll(responses, PCBA_ACK_YES) == 0) {
                    set_all_flags(s_app.pcba_low_power_ok, 1u);
                    enter_state(APP_STATE_SWITCH_5V);
                } else {
                    enter_state(APP_STATE_ERROR);
                }
            }
        }
        break;

    case APP_STATE_SWITCH_5V:
        AppPower_Enable5V();
        if (elapsed(APP_PCBA_POWER_SWITCH_DELAY_MS)) {
            enter_state(APP_STATE_NORMAL_POWER_QUERY);
        }
        break;

    case APP_STATE_NORMAL_POWER_QUERY:
        {
            if (s_app.single_pcba_mode != 0u) {
                AppPower_Enable5V();
                AppPower_Enable50mATestCircuit(1);
                {
                    uint8_t frame[PCBA_FRAME_MAX_SIZE];
                    size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_SINGLE_QUERY_NORMAL_POWER,
                                                          s_single_pcba_frame_channel,
                                                          frame,
                                                          sizeof(frame));
                    if (len != 0u &&
                        AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
                    enter_state(APP_STATE_PCBA_ZERO);
                    } else {
                        enter_state(APP_STATE_ERROR);
                    }
                }
            } else {
                PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
                if (AppPcbaUart_RequestAll(PCBA_CMD_QUERY_NORMAL_POWER, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                    AppPcbaUart_CheckOneByteAckAll(responses, PCBA_ACK_YES) == 0) {
                    set_all_flags(s_app.pcba_normal_power_ok, 1u);
                    enter_state(APP_STATE_CAL_50);
                } else {
                    enter_state(APP_STATE_ERROR);
                }
            }
        }
        break;

    case APP_STATE_CAL_50:
        if (s_app.single_pcba_mode != 0u) {
            single_pcba_protocol_cal_step(APP_PRESSURE_50_MMHG, APP_STATE_CAL_150);
        } else {
            pressure_cal_step(PRESSURE_SENSOR_TANK_50, APP_PRESSURE_50_MMHG, 2u, APP_STATE_CAL_150);
        }
        break;

    case APP_STATE_CAL_150:
        if (s_app.single_pcba_mode != 0u) {
            single_pcba_protocol_cal_step(APP_PRESSURE_150_MMHG, APP_STATE_CAL_250);
        } else {
            pressure_cal_step(PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG, 4u, APP_STATE_CAL_250);
        }
        break;

    case APP_STATE_CAL_250:
        if (s_app.single_pcba_mode != 0u) {
            single_pcba_protocol_cal_step(APP_PRESSURE_250_MMHG, APP_STATE_PCBA_WRITE_FLASH);
        } else {
            pressure_cal_step(PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG, 6u, APP_STATE_TEST_100);
        }
        break;

    case APP_STATE_TEST_100:
        if (s_app.single_pcba_mode != 0u) {
            pressure_test_step_single_pcba(PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG, 8u, APP_STATE_TEST_200);
        } else {
            pressure_test_step(PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG, 8u, APP_STATE_TEST_200);
        }
        break;

    case APP_STATE_TEST_200:
        if (s_app.single_pcba_mode != 0u) {
            pressure_test_step_single_pcba(PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG, 10u, APP_STATE_TEST_285);
        } else {
            pressure_test_step(PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG, 10u, APP_STATE_TEST_285);
        }
        break;

    case APP_STATE_TEST_285:
        if (s_app.single_pcba_mode != 0u) {
            pressure_test_step_single_pcba(PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG, 12u, APP_STATE_RESULT);
        } else {
            pressure_test_step(PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG, 12u, APP_STATE_RESULT);
        }
        break;

    case APP_STATE_RESULT:
        AppValves_AllClosed();
        if (s_app.single_pcba_mode != 0u) {
            AppPower_Enable45V();
            AppPower_Enable50mATestCircuit(0);
        } else if (s_app.step_sent == 0u) {
            s_app.step_sent = 1u;
        }
        if (s_app.single_pcba_mode == 0u &&
            AppKeys_Key2Pressed() &&
            elapsed(APP_RESULT_CONFIRM_KEY_DEBOUNCE_MS)) {
            enter_state(APP_STATE_REFILL);
        }
        break;

    case APP_STATE_REFILL:
        if (any_tank_sensor_fault_active() != 0u ||
            any_tank_pressure_above_default_limits_active() != 0u ||
            elapsed(APP_INIT_REFILL_MAX_RUNTIME_MS)) {
            enter_state(APP_STATE_ERROR);
            break;
        }
        refill_tanks();
        if (all_tanks_ready()) {
            enter_state(APP_STATE_READY);
        }
        break;

    case APP_STATE_ERROR:
        AppValves_AllClosed();
        AppPower_AllOff();
        break;

    case APP_STATE_PCBA_CURRENT_TEST:
        AppValves_AllClosed();
        pcba_current_test_task();
        break;

    case APP_STATE_RTC_DEBUG:
        AppValves_AllClosed();
        AppPower_AllOff();
        break;

    case APP_STATE_PCBA_TIMING_DIAGNOSTIC:
        if (s_pcba_timing_requested != 0u) {
            s_pcba_timing_requested = 0u;
            run_pcba_timing_diagnostic();
        } else {
            AppValves_AllClosed();
            AppPower_AllOff();
        }
        break;

    case APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC:
        if (s_single_tank_pcba_requested != 0u) {
            s_single_tank_pcba_requested = 0u;
            run_single_tank_pcba_diagnostic();
        } else {
            AppValves_AllClosed();
            AppPower_AllOff();
        }
        break;

    case APP_STATE_SINGLE_TANK_LOOP:
        AppPower_AllOff();
        single_tank_loop_task();
        break;

    case APP_STATE_SINGLE_PCBA_FLOW:
        enter_state(APP_STATE_PCBA_POWER_ON);
        break;

    case APP_STATE_PCBA_PRESSURE_QUERY:
        AppValves_AllClosed();
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        (void)AppPcbaUart_SendTestRoute(s_single_pcba_uart_route,
                                        s_single_pcba_frame_channel,
                                        &s_app.pcba_test_pressure[s_single_pcba_channel_index],
                                        APP_SINGLE_PCBA_RESPONSE_TIMEOUT_MS);
        enter_state(APP_STATE_CAL_50);
        break;

    case APP_STATE_PCBA_WRITE_FLASH:
        AppValves_AllClosed();
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (s_app.step_sent == 0u) {
            uint8_t frame[PCBA_FRAME_MAX_SIZE];
            size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_WRITE_FLASH,
                                                  s_single_pcba_frame_channel,
                                                  frame,
                                                  sizeof(frame));
            if (len != 0u &&
                AppPcbaUart_Send(s_single_pcba_uart_route, frame, (uint16_t)len) == 0) {
                s_app.step_sent = 1u;
                enter_state(APP_STATE_RESULT);
            } else {
                enter_state(APP_STATE_ERROR);
            }
        }
        break;

    case APP_STATE_SENSOR_CALIBRATION:
        AppPower_AllOff();
        clear_manual_valve_overrides();
        AppSensorCalibration_Task();
        break;

    default:
        AppValves_AllClosed();
        AppPower_AllOff();
        break;
    }

    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        apply_manual_valve_overrides();
    }
}

AppRuntimeState AppStateMachine_GetState(void)
{
    return s_app.state;
}

int AppStateMachine_RequestStart(void)
{
    if (s_app.state == APP_STATE_USB_MSC ||
        s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    if (AppPressureScope_HasBlockingFault(AppPressure_GetFaultLatchedMask(),
                                          APP_PRESSURE_TANK_SENSOR_MASK) != 0u) {
        AppValves_AllClosed();
        AppPower_AllOff();
        enter_state(APP_STATE_ERROR);
        return -1;
    }

    s_app.running = 1u;
    s_app.paused = 0u;
    s_app.single_pcba_mode = 0u;
    clear_manual_valve_overrides();
    reset_pcba_result_flags();

    if (s_app.state == APP_STATE_READY ||
        s_app.state == APP_STATE_RESULT ||
        s_app.state == APP_STATE_ERROR) {
        AppValves_AllClosed();
        AppPower_AllOff();
        enter_state(APP_STATE_INIT_TANKS);
    }

    return 0;
}

int AppStateMachine_RequestStop(void)
{
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }

    s_app.running = 0u;
    s_app.paused = 0u;
    s_app.pcba_current_50ma_enabled = 0u;
    s_app.single_pcba_mode = 0u;
    s_pcba_timing_stop_on_fail = 0u;
    s_single_tank_pcba_requested = 0u;
    s_single_tank_pcba_continue_on_fail = 0u;
    clear_manual_valve_overrides();
    reset_single_tank_loop_context();
    AppValves_AllClosed();
    AppPower_AllOff();
    enter_state(AppPressureScope_HasBlockingFault(AppPressure_GetFaultLatchedMask(),
                                                  APP_PRESSURE_TANK_SENSOR_MASK) != 0u ?
                APP_STATE_ERROR : APP_STATE_READY);
    return 0;
}

int AppStateMachine_RequestPause(void)
{
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }
    if (s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        (void)AppSensorCalibration_Jog(APP_SENSOR_CAL_ACTUATOR_STOP, 0u);
        AppValves_AllClosed();
        return -1;
    }
    s_app.paused = 1u;
    return 0;
}

int AppStateMachine_RequestResume(void)
{
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }
    s_app.paused = 0u;
    return 0;
}

int AppStateMachine_RequestState(AppRuntimeState state)
{
    if (state >= APP_STATE_COUNT || state == APP_STATE_USB_MSC ||
        state == APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    if (state_has_blocking_pressure_latch(state) != 0u && state != APP_STATE_ERROR) {
        AppValves_AllClosed();
        AppPower_AllOff();
        enter_state(APP_STATE_ERROR);
        return -1;
    }

    s_app.paused = 0u;
    if (state == APP_STATE_SINGLE_PCBA_FLOW) {
        s_app.running = 1u;
        s_app.single_pcba_mode = 1u;
        clear_manual_valve_overrides();
        reset_single_tank_loop_context();
        reset_pcba_result_flags();
        AppValves_AllClosed();
        AppPower_AllOff();
        enter_state(APP_STATE_PCBA_POWER_ON);
        return 0;
    }

    s_app.single_pcba_mode = 0u;
    s_app.running = (state != APP_STATE_READY && state != APP_STATE_ERROR) ? 1u : 0u;
    if (state != APP_STATE_SINGLE_TANK_LOOP) {
        reset_single_tank_loop_context();
    }
    enter_state(state);
    return 0;
}

const char *AppStateMachine_GetStateName(AppRuntimeState state)
{
    if (state >= APP_STATE_COUNT) {
        return "Unknown";
    }
    return s_state_names[state];
}

uint32_t AppStateMachine_GetStateElapsedMs(void)
{
    return HAL_GetTick() - s_app.entered_at;
}

uint8_t AppStateMachine_IsRunning(void)
{
    return s_app.running;
}

uint8_t AppStateMachine_IsPaused(void)
{
    return s_app.paused;
}

uint8_t AppStateMachine_IsManualMode(void)
{
    return s_app.manual_mode;
}

uint8_t AppStateMachine_IsError(void)
{
    return s_app.state == APP_STATE_ERROR ? 1u : 0u;
}

void AppStateMachine_SetManualMode(uint8_t enabled)
{
    if (s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        clear_manual_valve_overrides();
        return;
    }
    if (AppPressure_HasAnyFaultLatched() != 0 || s_app.state == APP_STATE_ERROR) {
        clear_manual_valve_overrides();
        return;
    }
    if (enabled != 0u) {
        s_app.manual_mode = 1u;
    } else {
        clear_manual_valve_overrides();
    }
}

void AppStateMachine_SetManualValve(uint8_t valve_number, uint8_t open)
{
    if (s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        clear_manual_valve_overrides();
        return;
    }
    if (AppPressure_HasAnyFaultLatched() != 0 || s_app.state == APP_STATE_ERROR) {
        clear_manual_valve_overrides();
        return;
    }
    if (valve_number < 1u || valve_number > 26u) {
        return;
    }

    uint32_t bit = (uint32_t)1u << (valve_number - 1u);
    s_app.manual_mode = 1u;
    s_app.manual_valve_override_mask |= bit;
    if (open != 0u) {
        s_app.manual_valve_open_mask |= bit;
    } else {
        s_app.manual_valve_open_mask &= ~bit;
    }
    AppValves_Set(valve_number, open != 0u ? 1u : 0u);
}

void AppStateMachine_SetManualValveMask(uint32_t valve_mask, uint32_t open_mask)
{
    if (s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        clear_manual_valve_overrides();
        return;
    }
    if (AppPressure_HasAnyFaultLatched() != 0 || s_app.state == APP_STATE_ERROR) {
        clear_manual_valve_overrides();
        return;
    }
    const uint32_t valid_mask = 0x03FFFFFFu;
    valve_mask &= valid_mask;
    open_mask &= valid_mask;
    if (valve_mask == 0u) {
        return;
    }

    s_app.manual_mode = 1u;
    s_app.manual_valve_override_mask |= valve_mask;
    s_app.manual_valve_open_mask =
        (s_app.manual_valve_open_mask & ~valve_mask) | (open_mask & valve_mask);

    for (uint8_t valve = 1u; valve <= 26u; ++valve) {
        const uint32_t bit = (uint32_t)1u << (valve - 1u);
        if ((valve_mask & bit) != 0u) {
            AppValves_Set(valve, (open_mask & bit) != 0u ? 1u : 0u);
        }
    }
}

void AppStateMachine_SetPressureTolerance001mmHg(uint32_t tolerance_001mmhg)
{
    s_app.pressure_tolerance_001mmhg = tolerance_001mmhg;
    if (s_app.single_tank_active != 0u && tolerance_001mmhg != 0u) {
        s_app.single_tank_tolerance_001mmhg = tolerance_001mmhg;
    }
}

uint32_t AppStateMachine_GetPressureTolerance001mmHg(void)
{
    return s_app.pressure_tolerance_001mmhg;
}

int AppStateMachine_RequestSingleTankLoop(uint8_t tank_index,
                                          uint32_t target_001mmhg,
                                          uint32_t tolerance_001mmhg)
{
    const TankLoopSpec *tank;

    if (tank_index >= APP_TANK_COUNT) {
        return -1;
    }
    tank = &s_tank_loop_specs[tank_index];
    if (pressure_sensor_fault_latched(tank->sensor) != 0u) {
        AppValves_AllClosed();
        AppPower_AllOff();
        s_app.single_tank_active = 1u;
        s_app.single_tank_index = tank_index;
        s_app.single_tank_target_001mmhg = target_001mmhg;
        s_app.single_tank_tolerance_001mmhg = tolerance_001mmhg;
        single_tank_trip_protection(tank_index, APP_SINGLE_TANK_PROTECTION_SENSOR_FAULT);
        s_app.running = 0u;
        return -1;
    }
    if (target_001mmhg == 0u || target_001mmhg > APP_MPRLS_PRESSURE_MAX_001MMHG) {
        return -1;
    }
    if (tolerance_001mmhg == 0u) {
        tolerance_001mmhg = 3u * APP_PRESSURE_SCALE_PER_MMHG;
    }
    if (tolerance_001mmhg > (20u * APP_PRESSURE_SCALE_PER_MMHG)) {
        return -1;
    }
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }

    clear_manual_valve_overrides();
    s_app.single_pcba_mode = 0u;
    reset_single_tank_loop_context();
    s_app.running = 1u;
    s_app.paused = 0u;
    s_app.single_tank_active = 1u;
    s_app.single_tank_index = tank_index;
    s_app.single_tank_target_001mmhg = target_001mmhg;
    s_app.single_tank_tolerance_001mmhg = tolerance_001mmhg;
    enter_state(APP_STATE_SINGLE_TANK_LOOP);
    s_app.step_sent = SINGLE_TANK_PHASE_IDLE;
    return 0;
}

int AppStateMachine_StopSingleTankLoop(void)
{
    if (s_app.state != APP_STATE_SINGLE_TANK_LOOP && s_app.single_tank_active == 0u) {
        return -1;
    }

    reset_single_tank_loop_context();
    s_app.running = 0u;
    s_app.paused = 0u;
    AppValves_AllClosed();
    AppPower_AllOff();
    enter_state(APP_STATE_READY);
    return 0;
}

uint8_t AppStateMachine_IsSingleTankLoopActive(void)
{
    return s_app.single_tank_active;
}

uint8_t AppStateMachine_IsSinglePcbaFlowActive(void)
{
    return s_app.single_pcba_mode;
}

uint8_t AppStateMachine_GetSingleTankIndex(void)
{
    return s_app.single_tank_index;
}

uint32_t AppStateMachine_GetSingleTankTarget001mmHg(void)
{
    return s_app.single_tank_target_001mmhg;
}

uint32_t AppStateMachine_GetSingleTankTolerance001mmHg(void)
{
    return s_app.single_tank_tolerance_001mmhg;
}

uint8_t AppStateMachine_IsSingleTankProtectionActive(void)
{
    return s_app.single_tank_protection_active;
}

uint8_t AppStateMachine_GetSingleTankProtectionReason(void)
{
    return s_app.single_tank_protection_reason;
}

uint8_t AppStateMachine_GetSingleTankProtectionTankIndex(void)
{
    return s_app.single_tank_protection_tank_index;
}

uint8_t AppStateMachine_GetSingleTankProtectionSensorIndex(void)
{
    return s_app.single_tank_protection_sensor_index;
}

uint8_t AppStateMachine_GetSingleTankProtectionInletValve(void)
{
    return s_app.single_tank_protection_inlet_valve;
}

void AppStateMachine_SetPcbaCurrent50mAEnabled(uint8_t enabled)
{
    s_app.pcba_current_50ma_enabled = enabled != 0u ? 1u : 0u;
    if (s_app.state == APP_STATE_PCBA_CURRENT_TEST) {
        AppPower_Enable50mATestCircuit(s_app.pcba_current_50ma_enabled);
        s_app.step_sent = 0u;
    }
}

uint8_t AppStateMachine_IsPcbaCurrent50mAEnabled(void)
{
    return s_app.pcba_current_50ma_enabled;
}

void AppStateMachine_SetPcbaSupply5VEnabled(uint8_t enabled)
{
    s_app.pcba_supply_5v_enabled = enabled != 0u ? 1u : 0u;
    if (s_app.state == APP_STATE_PCBA_CURRENT_TEST) {
        if (s_app.pcba_supply_5v_enabled != 0u) {
            AppPower_Enable5V();
        } else {
            AppPower_Enable45V();
        }
        s_app.step_sent = 0u;
    }
}

uint8_t AppStateMachine_IsPcbaSupply5VEnabled(void)
{
    return s_app.pcba_supply_5v_enabled;
}

uint8_t AppStateMachine_IsPcbaOnline(uint8_t channel)
{
    if (channel >= APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_app.pcba_online[channel];
}

uint8_t AppStateMachine_IsPcbaLowPowerOk(uint8_t channel)
{
    if (channel >= APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_app.pcba_low_power_ok[channel];
}

uint8_t AppStateMachine_IsPcbaNormalPowerOk(uint8_t channel)
{
    if (channel >= APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_app.pcba_normal_power_ok[channel];
}

uint32_t AppStateMachine_GetPcbaTestPressure001mmHg(uint8_t channel)
{
    if (channel >= APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_app.pcba_test_pressure[channel];
}

int AppStateMachine_RequestPcbaTimingDiagnostic(uint8_t stop_on_fail)
{
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }

    clear_manual_valve_overrides();
    s_app.single_pcba_mode = 0u;
    s_app.running = 1u;
    s_app.paused = 0u;
    s_pcba_timing_stop_on_fail = stop_on_fail != 0u ? 1u : 0u;
    s_pcba_timing_requested = 1u;
    reset_pcba_timing_report();
    enter_state(APP_STATE_PCBA_TIMING_DIAGNOSTIC);
    return 0;
}

void AppStateMachine_GetPcbaTimingReport(AppPcbaTimingReport *report)
{
    if (report == 0) {
        return;
    }
    *report = s_pcba_timing_report;
}

int AppStateMachine_RequestSingleTankPcbaDiagnostic(
    uint8_t continue_on_fail,
    uint32_t trend_max_residual_001mmhg,
    uint32_t trend_window_ms,
    uint32_t max_drop_rate_001mmhg_per_s)
{
    if (s_app.state == APP_STATE_USB_MSC ||
        s_app.state == APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC ||
        s_single_tank_pcba_requested != 0u ||
        s_single_tank_pcba_report.running != 0u) {
        return -1;
    }
    if (trend_max_residual_001mmhg <
            APP_SINGLE_TANK_PCBA_TREND_MAX_RESIDUAL_MIN_001MMHG ||
        trend_max_residual_001mmhg >
            APP_SINGLE_TANK_PCBA_TREND_MAX_RESIDUAL_MAX_001MMHG ||
        trend_window_ms < APP_SINGLE_TANK_PCBA_TREND_WINDOW_MIN_MS ||
        trend_window_ms > APP_SINGLE_TANK_PCBA_TREND_WINDOW_MAX_MS ||
        max_drop_rate_001mmhg_per_s <
            APP_SINGLE_TANK_PCBA_MAX_DROP_RATE_MIN_001MMHG_PER_S ||
        max_drop_rate_001mmhg_per_s >
            APP_SINGLE_TANK_PCBA_MAX_DROP_RATE_LIMIT_001MMHG_PER_S) {
        return -1;
    }

    clear_manual_valve_overrides();
    s_app.single_pcba_mode = 0u;
    s_app.running = 1u;
    s_app.paused = 0u;
    s_app.single_tank_pcba_trend_max_residual_001mmhg =
        trend_max_residual_001mmhg;
    s_app.single_tank_pcba_trend_window_ms = trend_window_ms;
    s_app.single_tank_pcba_max_drop_rate_001mmhg_per_s = max_drop_rate_001mmhg_per_s;
    s_single_tank_pcba_continue_on_fail = continue_on_fail != 0u ? 1u : 0u;
    s_single_tank_pcba_requested = 1u;
    reset_single_tank_pcba_report();
    enter_state(APP_STATE_SINGLE_TANK_PCBA_DIAGNOSTIC);
    return 0;
}

void AppStateMachine_GetSingleTankPcbaReport(AppSingleTankPcbaReport *report)
{
    if (report == 0) {
        return;
    }
    *report = s_single_tank_pcba_report;
}

uint32_t AppStateMachine_GetPcbaTimingPassCount(void)
{
    return s_pcba_timing_pass_count;
}

uint32_t AppStateMachine_GetPcbaTimingFailCount(void)
{
    return s_pcba_timing_fail_count;
}

uint8_t AppStateMachine_IsPcbaProbeServiceDue(void)
{
    const uint32_t total = s_pcba_timing_pass_count + s_pcba_timing_fail_count;

    return (total != 0u && (total % APP_PCBA_PROBE_SERVICE_INTERVAL) == 0u) ? 1u : 0u;
}

int AppStateMachine_RequestSensorCalibrationEnter(uint8_t in_place_mode,
                                                  uint8_t destination_slot)
{
    if (s_app.state == APP_STATE_USB_MSC) {
        return -1;
    }
    if (s_app.state == APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }

    s_app.running = 0u;
    s_app.paused = 0u;
    s_app.single_pcba_mode = 0u;
    clear_manual_valve_overrides();
    reset_single_tank_loop_context();
    AppPower_AllOff();
    AppValves_AllClosed();
    if (AppSensorCalibration_Enter(in_place_mode, destination_slot) != 0) {
        return -1;
    }
    enter_state(APP_STATE_SENSOR_CALIBRATION);
    return 0;
}

int AppStateMachine_RequestSensorCalibrationExit(void)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    AppValves_AllClosed();
    AppPower_AllOff();
    enter_state(AppPressure_HasAnyFaultLatched() != 0 ? APP_STATE_ERROR : APP_STATE_READY);
    return 0;
}

int AppStateMachine_SensorCalibrationJog(uint8_t actuator, uint16_t lease_ms)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_Jog(actuator, lease_ms);
}

int AppStateMachine_SensorCalibrationStartAutoVent(void)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_StartAutoVent();
}

int AppStateMachine_SensorCalibrationCancelAutoVent(void)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_CancelAutoVent();
}

int AppStateMachine_SensorCalibrationRecord(uint8_t point_index, uint32_t actual_001mmhg)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_Record(point_index, actual_001mmhg);
}

int AppStateMachine_SensorCalibrationSaveSlot(uint8_t slot)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_SaveSlot(slot);
}

int AppStateMachine_SensorCalibrationClearSlot(uint8_t slot)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_ClearSlot(slot);
}

int AppStateMachine_SensorCalibrationResetSession(void)
{
    if (s_app.state != APP_STATE_SENSOR_CALIBRATION) {
        return -1;
    }
    return AppSensorCalibration_ResetSession();
}
