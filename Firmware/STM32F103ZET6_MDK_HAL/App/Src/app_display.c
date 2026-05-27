#include "app_display.h"
#include "app_config.h"
#include "app_current.h"
#include "app_power.h"
#include "app_pressure.h"
#include "app_valves.h"
#include "lt768_basic.h"
#include "main.h"

#include <stdio.h>

#define DISPLAY_REFRESH_MS     250u
#define DISPLAY_TEXT_BG        LT768_COLOR_BLACK
#define DISPLAY_TITLE_BG       0x00004080u
#define DISPLAY_PANEL_BG       0x00181818u
#define DISPLAY_OK_COLOR       LT768_COLOR_GREEN
#define DISPLAY_WARN_COLOR     LT768_COLOR_YELLOW
#define DISPLAY_BAD_COLOR      LT768_COLOR_RED
#define DISPLAY_TEXT_COLOR     LT768_COLOR_WHITE
#define DISPLAY_DIM_COLOR      0x00989898u

typedef struct {
    const char *name;
    PressureSensorIndex sensor;
    uint32_t target;
} TankDisplayItem;

static const TankDisplayItem s_tanks[APP_TANK_COUNT] = {
    {"T50", PRESSURE_SENSOR_TANK_50, APP_PRESSURE_50_MMHG},
    {"T150", PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG},
    {"T250", PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG},
    {"T100", PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG},
    {"T200", PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG},
    {"T285", PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG}
};

static const PressureSensorIndex s_pcba_pressure_sensors[APP_PCBA_CHANNEL_COUNT] = {
    PRESSURE_SENSOR_CH1,
    PRESSURE_SENSOR_CH2,
    PRESSURE_SENSOR_CH3,
    PRESSURE_SENSOR_CH4,
    PRESSURE_SENSOR_CH5,
    PRESSURE_SENSOR_CH6,
    PRESSURE_SENSOR_CH7,
    PRESSURE_SENSOR_CH8
};

static uint8_t s_enabled;
static uint32_t s_last_refresh;

static uint32_t status_color(uint8_t ok)
{
    return ok ? DISPLAY_OK_COLOR : DISPLAY_DIM_COLOR;
}

static void draw_line(uint16_t x, uint16_t y, uint32_t color, const char *text)
{
    LT768_DrawText(x, y, color, DISPLAY_TEXT_BG, text);
}

static uint32_t pressure_whole(uint32_t pressure_001mmhg)
{
    return (pressure_001mmhg + 500u) / 1000u;
}

static void draw_static_layout(void)
{
    LT768_Clear(DISPLAY_TEXT_BG);
    LT768_FillRect(0u, 0u, 1023u, 39u, DISPLAY_TITLE_BG);
    LT768_FillRect(0u, 40u, 319u, 599u, DISPLAY_PANEL_BG);
    LT768_FillRect(320u, 40u, 1023u, 599u, DISPLAY_TEXT_BG);

    draw_line(16u, 10u, DISPLAY_TEXT_COLOR, "Air Pressure Testing Fixture");
    draw_line(16u, 54u, DISPLAY_TEXT_COLOR, "Steps");
    draw_line(344u, 54u, DISPLAY_TEXT_COLOR, "Power / Tanks / Valves / PCBA");
}

static void draw_steps(AppRuntimeState current)
{
    for (uint8_t i = 0u; i < APP_STATE_COUNT; ++i) {
        char line[48];
        const uint32_t color = (i == (uint8_t)current) ? DISPLAY_WARN_COLOR : DISPLAY_DIM_COLOR;
        (void)snprintf(line, sizeof(line), "%c%02u %s", (i == (uint8_t)current) ? '>' : ' ', (unsigned)(i + 1u), AppStateMachine_GetStateName((AppRuntimeState)i));
        draw_line(16u, (uint16_t)(80u + (i * 21u)), color, line);
    }
}

static void draw_power_and_tanks(void)
{
    char line[80];

    (void)snprintf(line,
                   sizeof(line),
                   "Power: 5V=%s  4.5V=%s  50mA=%s",
                   AppPower_Is5VEnabled() ? "ON " : "OFF",
                   AppPower_Is45VEnabled() ? "ON " : "OFF",
                   AppPower_Is50mATestCircuitEnabled() ? "ON " : "OFF");
    draw_line(344u, 86u, DISPLAY_TEXT_COLOR, line);

    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        const TankDisplayItem *tank = &s_tanks[i];
        const uint32_t pressure = AppPressure_Get001mmHg(tank->sensor);
        const uint8_t valid = (uint8_t)AppPressure_IsValid(tank->sensor);
        const uint8_t stable = (uint8_t)AppPressure_IsStable(tank->sensor, tank->target);
        (void)snprintf(line,
                       sizeof(line),
                       "%s target=%3lu now=%s%3lu mmHg %s",
                       tank->name,
                       (unsigned long)pressure_whole(tank->target),
                       valid ? "" : "?",
                       (unsigned long)pressure_whole(pressure),
                       stable ? "OK" : "WAIT");
        draw_line(344u, (uint16_t)(118u + (i * 21u)), stable ? DISPLAY_OK_COLOR : DISPLAY_TEXT_COLOR, line);
    }
}

static void draw_valves(void)
{
    char line[96];

    for (uint8_t row = 0u; row < 3u; ++row) {
        char *p = line;
        size_t left = sizeof(line);
        const uint8_t first = (uint8_t)(row * 9u + 1u);
        int written = snprintf(p, left, "V%02u-%02u:", (unsigned)first, (unsigned)((row == 2u) ? 26u : (first + 8u)));
        if (written < 0 || (size_t)written >= left) {
            line[sizeof(line) - 1u] = '\0';
        } else {
            p += written;
            left -= (size_t)written;
        }

        for (uint8_t i = 0u; i < 9u; ++i) {
            uint8_t valve = (uint8_t)(first + i);
            if (valve > 26u || left == 0u) {
                break;
            }
            written = snprintf(p, left, " %02u%s", (unsigned)valve, AppValves_IsOpen(valve) ? "O" : "-");
            if (written < 0 || (size_t)written >= left) {
                line[sizeof(line) - 1u] = '\0';
                break;
            }
            p += written;
            left -= (size_t)written;
        }
        draw_line(344u, (uint16_t)(260u + (row * 21u)), DISPLAY_TEXT_COLOR, line);
    }
}

static void draw_pcba(void)
{
    char line[112];

    draw_line(344u, 340u, DISPLAY_TEXT_COLOR, "PCBA  ON LOW NOR Standby Work  JigP RetP(mmHg)");
    for (uint8_t ch = 0u; ch < APP_PCBA_CHANNEL_COUNT; ++ch) {
        const uint32_t pressure = AppPressure_Get001mmHg(s_pcba_pressure_sensors[ch]);
        const uint8_t pressure_valid = (uint8_t)AppPressure_IsValid(s_pcba_pressure_sensors[ch]);
        const uint8_t online = AppStateMachine_IsPcbaOnline(ch);
        const uint8_t low_ok = AppStateMachine_IsPcbaLowPowerOk(ch);
        const uint8_t normal_ok = AppStateMachine_IsPcbaNormalPowerOk(ch);

        (void)snprintf(line,
                       sizeof(line),
                       "CH%u   %c   %c   %c %7lu %5lu  %s%3lu %3lu",
                       (unsigned)(ch + 1u),
                       online ? 'Y' : '-',
                       low_ok ? 'Y' : '-',
                       normal_ok ? 'Y' : '-',
                       (unsigned long)AppCurrent_GetStandbyUa(ch),
                       (unsigned long)AppCurrent_GetWorkUa(ch),
                       pressure_valid ? "" : "?",
                       (unsigned long)pressure_whole(pressure),
                       (unsigned long)pressure_whole(AppStateMachine_GetPcbaTestPressure001mmHg(ch)));
        draw_line(344u, (uint16_t)(366u + (ch * 21u)), status_color(online), line);
    }
}

void AppDisplay_Init(AppBootMode mode)
{
    s_enabled = (mode == APP_MODE_NORMAL) ? 1u : 0u;
    s_last_refresh = 0u;
    if (s_enabled != 0u) {
        draw_static_layout();
    }
}

void AppDisplay_Task(void)
{
    AppRuntimeState state;
    char line[96];

    if (s_enabled == 0u) {
        return;
    }

    if ((HAL_GetTick() - s_last_refresh) < DISPLAY_REFRESH_MS) {
        return;
    }
    s_last_refresh = HAL_GetTick();

    state = AppStateMachine_GetState();
    draw_static_layout();
    (void)snprintf(line,
                   sizeof(line),
                   "Current: %s   elapsed=%lu ms",
                   AppStateMachine_GetStateName(state),
                   (unsigned long)AppStateMachine_GetStateElapsedMs());
    draw_line(344u, 10u, DISPLAY_TEXT_COLOR, line);
    draw_steps(state);
    draw_power_and_tanks();
    draw_valves();
    draw_pcba();
}
