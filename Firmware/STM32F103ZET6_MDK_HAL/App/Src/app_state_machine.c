#include "app_state_machine.h"
#include "app_config.h"
#include "app_current.h"
#include "app_keys.h"
#include "app_pcba_uart.h"
#include "app_power.h"
#include "app_pressure.h"
#include "app_protocol.h"
#include "app_valves.h"
#include "board_pins.h"
#include "main.h"
#include "usb_msc_app.h"

typedef enum {
    ST_USB_MSC = 0,
    ST_INIT_TANKS,
    ST_AUTO_AIRTIGHTNESS,
    ST_READY,
    ST_PCBA_POWER_ON,
    ST_PCBA_STANDBY_CURRENT_CHECK,
    ST_PCBA_WAKE,
    ST_PCBA_WORK_CURRENT_MEASURE,
    ST_PCBA_SET_TEST_MODE,
    ST_PCBA_ZERO,
    ST_SWITCH_45V,
    ST_LOW_POWER_QUERY,
    ST_SWITCH_5V,
    ST_NORMAL_POWER_QUERY,
    ST_CAL_50,
    ST_CAL_150,
    ST_CAL_250,
    ST_TEST_100,
    ST_TEST_200,
    ST_TEST_285,
    ST_RESULT,
    ST_REFILL,
    ST_ERROR
} AppState;

typedef struct {
    AppState state;
    uint32_t entered_at;
    uint8_t step_sent;
    uint32_t airtight_start[APP_TANK_COUNT];
    uint32_t pcba_test_pressure[APP_PCBA_CHANNEL_COUNT];
} AppContext;

static AppContext s_app;

static const uint8_t s_channel_valves[8] = {13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u};

static void enter_state(AppState state)
{
    s_app.state = state;
    s_app.entered_at = HAL_GetTick();
    s_app.step_sent = 0u;
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

static void refill_tanks(void)
{
    static const uint8_t inlet_valves[6] = {1u, 3u, 5u, 7u, 9u, 11u};

    AppValves_AllClosed();
    AppValves_OpenMask(inlet_valves, sizeof(inlet_valves));
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

static uint8_t all_tanks_ready(void)
{
    return AppPressure_IsStable(PRESSURE_SENSOR_TANK_50, APP_PRESSURE_50_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG) &&
           AppPressure_IsStable(PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG);
}

static uint8_t pressure_step_ready(PressureSensorIndex sensor, uint32_t target)
{
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
                              AppState next)
{
    if (s_app.step_sent == 0u) {
        open_output_to_all_channels(outlet_valve);
        s_app.step_sent = 1u;
    }
    if (pressure_step_ready(sensor, target)) {
        uint32_t real_pressure = AppPressure_Get001mmHg(sensor);
        PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
        if (AppPcbaUart_SendPressureAll(PCBA_CMD_SYNC_PRESSURE_CAL,
                                        real_pressure,
                                        responses,
                                        APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
            AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
            enter_state(next);
        } else {
            enter_state(ST_ERROR);
        }
    } else if (elapsed(APP_STAGE_SETTLE_TIMEOUT_MS)) {
        enter_state(ST_ERROR);
    }
}

static void pressure_test_step(PressureSensorIndex sensor,
                               uint32_t target,
                               uint8_t outlet_valve,
                               AppState next)
{
    if (s_app.step_sent == 0u) {
        open_output_to_all_channels(outlet_valve);
        s_app.step_sent = 1u;
    }
    if (pressure_step_ready(sensor, target)) {
        if (AppPcbaUart_SendTestAll(s_app.pcba_test_pressure, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0) {
            enter_state(next);
        } else {
            enter_state(ST_ERROR);
        }
    } else if (elapsed(APP_STAGE_SETTLE_TIMEOUT_MS)) {
        enter_state(ST_ERROR);
    }
}

void AppStateMachine_Init(AppBootMode mode)
{
    AppPower_AllOff();
    AppValves_AllClosed();
    AppCurrent_Init();
    AppPressure_Init();
    AppPcbaUart_Init();

    if (mode == APP_MODE_USB_MSC) {
        BoardPins_ConfigSpi3Msc();
        (void)UsbMscApp_Start();
        enter_state(ST_USB_MSC);
    } else {
        BoardPins_ConfigSpi3Float();
        enter_state(ST_INIT_TANKS);
    }
}

void AppStateMachine_Task(void)
{
    AppPressure_Task();

    switch (s_app.state) {
    case ST_USB_MSC:
        UsbMscApp_Task();
        break;

    case ST_INIT_TANKS:
        refill_tanks();
        if (all_tanks_ready()) {
            enter_state(ST_AUTO_AIRTIGHTNESS);
        }
        break;

    case ST_AUTO_AIRTIGHTNESS:
        AppValves_AllClosed();
        if (s_app.step_sent == 0u) {
            capture_airtight_start();
            s_app.step_sent = 1u;
        }
        if (elapsed(APP_AIRTIGHTNESS_HOLD_TIME_MS)) {
            enter_state(airtight_drop_ok() ? ST_READY : ST_ERROR);
        }
        break;

    case ST_READY:
        AppValves_AllClosed();
        if (AppKeys_PressSwitchActive()) {
            enter_state(ST_PCBA_POWER_ON);
        }
        break;

    case ST_PCBA_POWER_ON:
        AppPower_Enable5V();
        if (elapsed(1000u)) {
            AppPower_Enable50mATestCircuit(0);
            enter_state(ST_PCBA_STANDBY_CURRENT_CHECK);
        }
        break;

    case ST_PCBA_STANDBY_CURRENT_CHECK:
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(0);
        if (standby_current_check_done()) {
            enter_state(ST_PCBA_WAKE);
        }
        break;

    case ST_PCBA_WAKE: {
        PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (AppPcbaUart_WakeAll(responses, APP_PCBA_WAKE_RESPONSE_TIMEOUT_MS) == 0 &&
            AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
            enter_state(ST_PCBA_WORK_CURRENT_MEASURE);
        } else {
            enter_state(ST_ERROR);
        }
        break;
    }

    case ST_PCBA_WORK_CURRENT_MEASURE:
        AppPower_Enable5V();
        AppPower_Enable50mATestCircuit(1);
        if (work_current_measure_done()) {
            enter_state(ST_PCBA_SET_TEST_MODE);
        }
        break;

    case ST_PCBA_SET_TEST_MODE:
        {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_SendCommandAll(PCBA_CMD_SET_TEST_MODE, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
                enter_state(ST_PCBA_ZERO);
            } else {
                enter_state(ST_ERROR);
            }
        }
        break;

    case ST_PCBA_ZERO:
        AppValves_AllClosed();
        {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_SendCommandAll(PCBA_CMD_RECORD_ZERO_AD, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckEmptyAckAll(responses) == 0) {
                enter_state(ST_SWITCH_45V);
            } else {
                enter_state(ST_ERROR);
            }
        }
        break;

    case ST_SWITCH_45V:
        AppPower_Enable45V();
        if (elapsed(APP_PCBA_POWER_SWITCH_DELAY_MS)) {
            enter_state(ST_LOW_POWER_QUERY);
        }
        break;

    case ST_LOW_POWER_QUERY:
        {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_RequestAll(PCBA_CMD_QUERY_LOW_POWER_STATE, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckOneByteAckAll(responses, PCBA_ACK_YES) == 0) {
                enter_state(ST_SWITCH_5V);
            } else {
                enter_state(ST_ERROR);
            }
        }
        break;

    case ST_SWITCH_5V:
        AppPower_Enable5V();
        if (elapsed(APP_PCBA_POWER_SWITCH_DELAY_MS)) {
            enter_state(ST_NORMAL_POWER_QUERY);
        }
        break;

    case ST_NORMAL_POWER_QUERY:
        {
            PcbaFrame responses[APP_PCBA_CHANNEL_COUNT];
            if (AppPcbaUart_RequestAll(PCBA_CMD_QUERY_NORMAL_POWER, responses, APP_PCBA_RESPONSE_TIMEOUT_MS) == 0 &&
                AppPcbaUart_CheckOneByteAckAll(responses, PCBA_ACK_YES) == 0) {
                enter_state(ST_CAL_50);
            } else {
                enter_state(ST_ERROR);
            }
        }
        break;

    case ST_CAL_50:
        pressure_cal_step(PRESSURE_SENSOR_TANK_50, APP_PRESSURE_50_MMHG, 2u, ST_CAL_150);
        break;

    case ST_CAL_150:
        pressure_cal_step(PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG, 4u, ST_CAL_250);
        break;

    case ST_CAL_250:
        pressure_cal_step(PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG, 6u, ST_TEST_100);
        break;

    case ST_TEST_100:
        pressure_test_step(PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG, 8u, ST_TEST_200);
        break;

    case ST_TEST_200:
        pressure_test_step(PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG, 10u, ST_TEST_285);
        break;

    case ST_TEST_285:
        pressure_test_step(PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG, 12u, ST_RESULT);
        break;

    case ST_RESULT:
        AppValves_AllClosed();
        if (s_app.step_sent == 0u) {
            s_app.step_sent = 1u;
        }
        if (AppKeys_Key2Pressed() && elapsed(APP_RESULT_CONFIRM_KEY_DEBOUNCE_MS)) {
            enter_state(ST_REFILL);
        }
        break;

    case ST_REFILL:
        refill_tanks();
        if (all_tanks_ready()) {
            enter_state(ST_READY);
        }
        break;

    case ST_ERROR:
    default:
        AppValves_AllClosed();
        AppPower_AllOff();
        break;
    }
}
