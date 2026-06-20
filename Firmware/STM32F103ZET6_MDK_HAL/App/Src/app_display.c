#include "app_display.h"
#include "app_config.h"
#include "app_current.h"
#include "app_power.h"
#include "app_pressure.h"
#include "app_valves.h"
#include "lt768_basic.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define DISPLAY_POLL_MS           200u

#define UI_BG                     0x00F4F7FBu
#define UI_PAGE                   0x00FFFFFFu
#define UI_PANEL                  0x00EEF4FCu
#define UI_PANEL_2                0x00E6EEF8u
#define UI_HEADER                 0x008ECFE8u
#define UI_CARD                   0x00F8FBFFu
#define UI_CARD_BLUE              0x00E6F0FFu
#define UI_CARD_GREEN             0x00E5F8EFu
#define UI_CARD_AMBER             0x00FFF3D6u
#define UI_CARD_RED               0x00FFE5E5u
#define UI_STROKE                 0x00879BB3u
#define UI_STROKE_SOFT            0x00B9C7D8u
#define UI_PIPE                   0x008A98A8u
#define UI_PIPE_ACTIVE            0x0020A96Bu
#define UI_TEXT                   0x00152435u
#define UI_TEXT_DIM               0x005B6B7Du
#define UI_ACCENT                 0x002D7DFFu
#define UI_OK                     0x0022AA66u
#define UI_WARN                   0x00CE8A00u
#define UI_BAD                    0x00D04040u

#define LEFT_X                    8u
#define LEFT_Y                    44u
#define LEFT_W                    56u
#define LEFT_H                    548u
#define RIGHT_X                   72u
#define RIGHT_Y                   44u
#define RIGHT_W                   944u
#define RIGHT_H                   548u
#define RIGHT_PAD                 14u
#define RIGHT_BODY_W              (RIGHT_W - (RIGHT_PAD * 2u))
#define FLOW_STEP_X               (LEFT_X + 9u)
#define FLOW_STEP_Y0              58u
#define FLOW_STEP_GAP             36u
#define FLOW_STEP_W               38u
#define FLOW_STEP_H               28u
#define DISPLAY_VALVE_COUNT       26u
#define DISPLAY_CACHE_U8_INVALID  0xFFu
#define DISPLAY_CACHE_U32_INVALID 0xFFFFFFFFu

typedef struct {
    AppRuntimeState first;
    AppRuntimeState last;
} FlowStep;

typedef struct {
    const char *name;
    PressureSensorIndex sensor;
    uint32_t target;
    uint8_t inlet_valve;
    uint8_t outlet_valve;
    uint8_t relief_valve;
} TankDisplayItem;

typedef struct {
    uint8_t valve;
    PressureSensorIndex sensor;
} PcbaDisplayItem;

static const FlowStep s_flow_steps[] = {
    {APP_STATE_INIT_TANKS,                 APP_STATE_INIT_TANKS},
    {APP_STATE_AUTO_AIRTIGHTNESS,          APP_STATE_AUTO_AIRTIGHTNESS},
    {APP_STATE_READY,                      APP_STATE_READY},
    {APP_STATE_PCBA_POWER_ON,              APP_STATE_PCBA_POWER_ON},
    {APP_STATE_PCBA_STANDBY_CURRENT_CHECK, APP_STATE_PCBA_STANDBY_CURRENT_CHECK},
    {APP_STATE_PCBA_WAKE,                  APP_STATE_PCBA_WAKE},
    {APP_STATE_PCBA_WORK_CURRENT_MEASURE,  APP_STATE_PCBA_WORK_CURRENT_MEASURE},
    {APP_STATE_PCBA_ZERO,                  APP_STATE_PCBA_ZERO},
    {APP_STATE_SWITCH_45V,                 APP_STATE_LOW_POWER_QUERY},
    {APP_STATE_SWITCH_5V,                  APP_STATE_NORMAL_POWER_QUERY},
    {APP_STATE_CAL_50,                     APP_STATE_CAL_250},
    {APP_STATE_TEST_100,                   APP_STATE_TEST_285},
    {APP_STATE_RESULT,                     APP_STATE_RESULT},
    {APP_STATE_REFILL,                     APP_STATE_ERROR}
};

static const TankDisplayItem s_tanks[APP_TANK_COUNT] = {
    {"T50",  PRESSURE_SENSOR_TANK_50,  APP_PRESSURE_50_MMHG,  1u,  2u, 21u},
    {"T150", PRESSURE_SENSOR_TANK_150, APP_PRESSURE_150_MMHG, 3u,  4u, 22u},
    {"T250", PRESSURE_SENSOR_TANK_250, APP_PRESSURE_250_MMHG, 5u,  6u, 23u},
    {"T100", PRESSURE_SENSOR_TANK_100, APP_PRESSURE_100_MMHG, 7u,  8u, 24u},
    {"T200", PRESSURE_SENSOR_TANK_200, APP_PRESSURE_200_MMHG, 9u, 10u, 25u},
    {"T285", PRESSURE_SENSOR_TANK_285, APP_PRESSURE_285_MMHG, 11u, 12u, 26u}
};

static const PcbaDisplayItem s_pcba[APP_PCBA_CHANNEL_COUNT] = {
    {13u, PRESSURE_SENSOR_CH1},
    {14u, PRESSURE_SENSOR_CH2},
    {15u, PRESSURE_SENSOR_CH3},
    {16u, PRESSURE_SENSOR_CH4},
    {17u, PRESSURE_SENSOR_CH5},
    {18u, PRESSURE_SENSOR_CH6},
    {19u, PRESSURE_SENSOR_CH7},
    {20u, PRESSURE_SENSOR_CH8}
};

static const uint16_t s_tank_x[APP_TANK_COUNT] = {134u, 220u, 306u, 392u, 478u, 564u};
static const uint16_t s_pcba_x[APP_PCBA_CHANNEL_COUNT] = {650u, 694u, 738u, 782u, 826u, 870u, 914u, 958u};

typedef struct {
    AppRuntimeState state;
    uint8_t power_bits;
    uint8_t valve_open[DISPLAY_VALVE_COUNT + 1u];
    uint32_t tank_fill[APP_TANK_COUNT];
    uint32_t tank_sensor_fill[APP_TANK_COUNT];
    uint32_t channel_fill[APP_PCBA_CHANNEL_COUNT];
    uint32_t channel_sensor_fill[APP_PCBA_CHANNEL_COUNT];
    uint32_t tank_pressure[APP_TANK_COUNT];
    uint8_t tank_pressure_valid[APP_TANK_COUNT];
    uint32_t tank_summary_fill[APP_TANK_COUNT];
    uint32_t channel_pressure[APP_PCBA_CHANNEL_COUNT];
    uint32_t channel_standby_x100[APP_PCBA_CHANNEL_COUNT];
    uint32_t channel_summary_fill[APP_PCBA_CHANNEL_COUNT];
} DisplayCache;

static uint8_t s_enabled;
static uint32_t s_last_refresh;
static int8_t s_last_flow_step = -1;
static DisplayCache s_cache;

static void display_cache_invalidate(void)
{
    (void)memset(&s_cache, DISPLAY_CACHE_U8_INVALID, sizeof(s_cache));
    s_cache.state = APP_STATE_COUNT;
}

static uint32_t pressure_whole(uint32_t pressure_001mmhg)
{
    return (pressure_001mmhg + 500u) / 1000u;
}

static uint32_t current_ua_x100(float current_ua)
{
    if (current_ua <= 0.0f) {
        return 0u;
    }
    if (current_ua >= 42949672.0f) {
        return 4294967295u;
    }
    return (uint32_t)((current_ua * 100.0f) + 0.5f);
}

static void format_current_x100(char *buffer, size_t buffer_size, uint32_t current_x100)
{
    if (buffer_size == 0u) {
        return;
    }
    (void)snprintf(buffer,
                   buffer_size,
                   "%lu.%02lu",
                   (unsigned long)(current_x100 / 100u),
                   (unsigned long)(current_x100 % 100u));
}

static void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    if (w == 0u || h == 0u) {
        return;
    }
    LT768_FillRect(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u), color);
}

static void draw_text(uint16_t x, uint16_t y, uint32_t color, uint32_t bg, const char *text)
{
    LT768_DrawText(x, y, color, bg, text);
}

static void draw_panel(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t fill, uint32_t stroke)
{
    draw_rect(x, y, w, h, stroke);
    if (w > 2u && h > 2u) {
        draw_rect((uint16_t)(x + 1u), (uint16_t)(y + 1u), (uint16_t)(w - 2u), (uint16_t)(h - 2u), fill);
    }
}

static void draw_label_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t fill, uint32_t stroke, const char *text)
{
    draw_panel(x, y, w, h, fill, stroke);
    draw_text((uint16_t)(x + 5u), (uint16_t)(y + ((h > 20u) ? ((h - 16u) / 2u) : 2u)), UI_TEXT, fill, text);
}

static void draw_two_line_box(uint16_t x,
                              uint16_t y,
                              uint16_t w,
                              uint16_t h,
                              uint32_t fill,
                              uint32_t stroke,
                              const char *line1,
                              const char *line2)
{
    draw_panel(x, y, w, h, fill, stroke);
    draw_text((uint16_t)(x + 6u), (uint16_t)(y + 7u), UI_TEXT, fill, line1);
    draw_text((uint16_t)(x + 6u), (uint16_t)(y + 28u), UI_TEXT_DIM, fill, line2);
}

static void draw_pipe_h(uint16_t x1, uint16_t x2, uint16_t y, uint16_t thick, uint32_t color)
{
    if (x2 < x1) {
        uint16_t tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    draw_rect(x1, (uint16_t)(y - (thick / 2u)), (uint16_t)(x2 - x1 + 1u), thick, color);
}

static void draw_pipe_v(uint16_t x, uint16_t y1, uint16_t y2, uint16_t thick, uint32_t color)
{
    if (y2 < y1) {
        uint16_t tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    draw_rect((uint16_t)(x - (thick / 2u)), y1, thick, (uint16_t)(y2 - y1 + 1u), color);
}

static int8_t flow_step_for_state(AppRuntimeState state)
{
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(s_flow_steps) / sizeof(s_flow_steps[0])); ++i) {
        if (state >= s_flow_steps[i].first && state <= s_flow_steps[i].last) {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint32_t tank_fill(const TankDisplayItem *tank)
{
    if (!AppPressure_IsValid(tank->sensor)) {
        return UI_CARD;
    }
    return AppPressure_IsStable(tank->sensor, tank->target) ? UI_CARD_BLUE : UI_CARD_AMBER;
}

static uint32_t valve_fill(uint8_t valve)
{
    return AppValves_IsOpen(valve) ? UI_CARD_GREEN : UI_PANEL_2;
}

static uint32_t valve_stroke(uint8_t valve)
{
    return AppValves_IsOpen(valve) ? UI_OK : UI_STROKE;
}

static uint32_t sensor_fill(PressureSensorIndex sensor)
{
    return AppPressure_IsValid(sensor) ? UI_CARD_AMBER : UI_CARD;
}

static uint32_t channel_fill(uint8_t channel)
{
    if (AppStateMachine_IsPcbaOnline(channel) == 0u) {
        return UI_CARD;
    }
    if (AppStateMachine_GetState() == APP_STATE_RESULT) {
        return (AppStateMachine_IsPcbaLowPowerOk(channel) && AppStateMachine_IsPcbaNormalPowerOk(channel)) ?
               UI_CARD_GREEN :
               UI_CARD_RED;
    }
    return UI_CARD_GREEN;
}

static void draw_flow_step(uint8_t index, uint8_t active)
{
    const uint16_t y = (uint16_t)(FLOW_STEP_Y0 + (index * FLOW_STEP_GAP));
    const unsigned step = (unsigned)(index + 1u);
    const uint32_t fill = active ? UI_CARD_GREEN : UI_PAGE;
    const uint32_t stroke = active ? UI_OK : UI_STROKE_SOFT;
    const uint32_t text = active ? UI_TEXT : UI_TEXT_DIM;
    const uint16_t text_x = (uint16_t)(FLOW_STEP_X + ((step < 10u) ? 15u : 11u));
    char label[4];

    (void)snprintf(label, sizeof(label), "%u", step);
    draw_panel(FLOW_STEP_X, y, FLOW_STEP_W, FLOW_STEP_H, fill, stroke);
    draw_text(text_x, (uint16_t)(y + 7u), text, fill, label);
}

static void draw_flow_column(uint8_t force)
{
    const AppRuntimeState state = AppStateMachine_GetState();
    const int8_t active = flow_step_for_state(state);

    if (force == 0u && active == s_last_flow_step) {
        return;
    }

    if (force != 0u) {
        draw_panel(LEFT_X, LEFT_Y, LEFT_W, LEFT_H, UI_PAGE, UI_STROKE_SOFT);
        for (uint8_t i = 0u; i < (uint8_t)(sizeof(s_flow_steps) / sizeof(s_flow_steps[0])); ++i) {
            draw_flow_step(i, (active == (int8_t)i) ? 1u : 0u);
        }
    } else {
        if (s_last_flow_step >= 0) {
            draw_flow_step((uint8_t)s_last_flow_step, 0u);
        }
        if (active >= 0) {
            draw_flow_step((uint8_t)active, 1u);
        }
    }

    s_last_flow_step = active;
}

static void draw_static_shell(void)
{
    draw_rect(0u, 0u, LT768_SCREEN_WIDTH, LT768_SCREEN_HEIGHT, UI_BG);
    draw_rect(0u, 0u, LT768_SCREEN_WIDTH, 36u, UI_HEADER);
    draw_text(14u, 10u, UI_TEXT, UI_HEADER, "Air Pressure Fixture");
    draw_text(800u, 10u, UI_TEXT_DIM, UI_HEADER, "Local Production UI");

    draw_panel(RIGHT_X, RIGHT_Y, RIGHT_W, RIGHT_H, UI_PAGE, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 14u), 58u, UI_TEXT, UI_PAGE, "Fixture Work Status");
    draw_text((uint16_t)(RIGHT_X + RIGHT_W - 82u), 58u, UI_TEXT_DIM, UI_PAGE, "Ready");

    draw_panel((uint16_t)(RIGHT_X + RIGHT_PAD), 86u, RIGHT_BODY_W, 32u, UI_PANEL, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 24u), 94u, UI_TEXT_DIM, UI_PANEL, "Air source -> internal manifold -> tanks / PCBA channels -> shared pressure bus");

    draw_panel((uint16_t)(RIGHT_X + RIGHT_PAD), 128u, RIGHT_BODY_W, 272u, UI_BG, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 24u), 138u, UI_TEXT_DIM, UI_BG, "Pneumatic topology");

    draw_panel((uint16_t)(RIGHT_X + RIGHT_PAD), 412u, RIGHT_BODY_W, 70u, UI_PANEL, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 24u), 423u, UI_TEXT_DIM, UI_PANEL, "Tank pressure summary");

    draw_panel((uint16_t)(RIGHT_X + RIGHT_PAD), 494u, RIGHT_BODY_W, 86u, UI_PANEL, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 24u), 505u, UI_TEXT_DIM, UI_PANEL, "PCBA channel summary");

    draw_flow_column(1u);
}

static uint8_t status_power_bits(void)
{
    uint8_t bits = 0u;

    bits |= AppPower_Is5VEnabled() ? 0x01u : 0u;
    bits |= AppPower_Is45VEnabled() ? 0x02u : 0u;
    bits |= AppPower_Is50mATestCircuitEnabled() ? 0x04u : 0u;
    bits |= AppStateMachine_IsRunning() ? 0x08u : 0u;
    return bits;
}

static void draw_status_bar(uint8_t force)
{
    char line[108];
    const AppRuntimeState state = AppStateMachine_GetState();
    const uint8_t power_bits = status_power_bits();

    if (force != 0u || s_cache.state != state) {
        draw_rect((uint16_t)(RIGHT_X + 190u), 52u, (uint16_t)(RIGHT_W - 300u), 24u, UI_PAGE);
        (void)snprintf(line, sizeof(line), "STATE %-22s", AppStateMachine_GetStateName(state));
        draw_text((uint16_t)(RIGHT_X + 190u), 58u, UI_TEXT, UI_PAGE, line);
        s_cache.state = state;
    }

    if (force != 0u || s_cache.power_bits != power_bits) {
        draw_rect((uint16_t)(RIGHT_X + 24u), 94u, (uint16_t)(RIGHT_BODY_W - 48u), 18u, UI_PANEL);
        (void)snprintf(line,
                       sizeof(line),
                       "Power: 5V=%s   4.5V=%s   50mA=%s   Running=%s",
                       AppPower_Is5VEnabled() ? "ON " : "OFF",
                       AppPower_Is45VEnabled() ? "ON " : "OFF",
                       AppPower_Is50mATestCircuitEnabled() ? "ON " : "OFF",
                       AppStateMachine_IsRunning() ? "YES" : "NO ");
        draw_text((uint16_t)(RIGHT_X + 24u), 94u, UI_TEXT, UI_PANEL, line);
        s_cache.power_bits = power_bits;
    }
}

static void draw_valve(uint16_t x, uint16_t y, uint8_t valve)
{
    char label[8];

    (void)snprintf(label, sizeof(label), "V%02u", (unsigned)valve);
    draw_label_box(x, y, 38u, 24u, valve_fill(valve), valve_stroke(valve), label);
}

static void draw_topology_static(void)
{
    draw_panel((uint16_t)(RIGHT_X + RIGHT_PAD), 128u, RIGHT_BODY_W, 272u, UI_BG, UI_STROKE_SOFT);
    draw_text((uint16_t)(RIGHT_X + 24u), 138u, UI_TEXT_DIM, UI_BG, "Pneumatic topology");

    draw_two_line_box(96u, 172u, 44u, 52u, UI_CARD_BLUE, UI_STROKE, "AIR", "SRC");
    draw_pipe_h(140u, 1000u, 192u, 6u, UI_PIPE);
    draw_label_box(760u, 176u, 200u, 30u, UI_PANEL, UI_STROKE, "Internal manifold");

    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        const uint16_t x = s_tank_x[i];
        draw_label_box((uint16_t)(x + 2u), 226u, 48u, 26u, UI_PANEL, UI_STROKE, "REG");
    }

    draw_pipe_h(118u, 1000u, 396u, 5u, UI_PIPE);
}

static void draw_tank_topology(uint8_t index, uint8_t force)
{
    const TankDisplayItem *tank = &s_tanks[index];
    const uint16_t x = s_tank_x[index];
    const uint16_t cx = (uint16_t)(x + 26u);
    const uint8_t inlet_open = AppValves_IsOpen(tank->inlet_valve);
    const uint8_t outlet_open = AppValves_IsOpen(tank->outlet_valve);
    const uint8_t relief_open = AppValves_IsOpen(tank->relief_valve);
    const uint32_t inlet_pipe = inlet_open ? UI_PIPE_ACTIVE : UI_PIPE;
    const uint32_t outlet_pipe = outlet_open ? UI_PIPE_ACTIVE : UI_PIPE;
    const uint32_t relief_pipe = relief_open ? UI_PIPE_ACTIVE : UI_PIPE;
    const uint32_t tank_color = tank_fill(tank);
    const uint32_t sensor_color = sensor_fill(tank->sensor);
    const uint8_t redraw = (force != 0u ||
                            s_cache.valve_open[tank->inlet_valve] != inlet_open ||
                            s_cache.valve_open[tank->outlet_valve] != outlet_open ||
                            s_cache.valve_open[tank->relief_valve] != relief_open ||
                            s_cache.tank_fill[index] != tank_color ||
                            s_cache.tank_sensor_fill[index] != sensor_color) ? 1u : 0u;
    char label[16];

    if (redraw == 0u) {
        return;
    }

    draw_pipe_v(cx, 192u, 226u, 4u, inlet_pipe);
    draw_label_box((uint16_t)(x + 2u), 226u, 48u, 26u, UI_PANEL, UI_STROKE, "REG");
    draw_pipe_v(cx, 252u, 268u, 4u, inlet_pipe);
    draw_valve((uint16_t)(x + 7u), 268u, tank->inlet_valve);
    draw_pipe_v(cx, 292u, 308u, 4u, inlet_pipe);

    draw_label_box(x, 308u, 54u, 32u, tank_color, UI_ACCENT, tank->name);

    draw_pipe_v((uint16_t)(x + 12u), 340u, 346u, 3u, relief_pipe);
    draw_valve((uint16_t)(x - 11u), 346u, tank->relief_valve);
    draw_pipe_v((uint16_t)(x + 47u), 340u, 346u, 3u, UI_PIPE);
    (void)snprintf(label, sizeof(label), "P%u", (unsigned)(index + 1u));
    draw_label_box((uint16_t)(x + 28u), 346u, 38u, 24u, sensor_color, UI_WARN, label);

    draw_pipe_v(cx, 340u, 372u, 4u, outlet_pipe);
    draw_valve((uint16_t)(x + 8u), 372u, tank->outlet_valve);
    draw_pipe_v(cx, 394u, 396u, 4u, outlet_pipe);

    s_cache.valve_open[tank->inlet_valve] = inlet_open;
    s_cache.valve_open[tank->outlet_valve] = outlet_open;
    s_cache.valve_open[tank->relief_valve] = relief_open;
    s_cache.tank_fill[index] = tank_color;
    s_cache.tank_sensor_fill[index] = sensor_color;
}

static void draw_pcba_topology(uint8_t channel, uint8_t force)
{
    const uint16_t x = s_pcba_x[channel];
    const uint16_t cx = (uint16_t)(x + 19u);
    const uint8_t valve = s_pcba[channel].valve;
    const uint8_t open = AppValves_IsOpen(valve);
    const uint32_t pipe = open ? UI_PIPE_ACTIVE : UI_PIPE;
    const uint32_t card_color = channel_fill(channel);
    const uint32_t pressure_color = sensor_fill(s_pcba[channel].sensor);
    const uint8_t redraw = (force != 0u ||
                            s_cache.valve_open[valve] != open ||
                            s_cache.channel_fill[channel] != card_color ||
                            s_cache.channel_sensor_fill[channel] != pressure_color) ? 1u : 0u;
    char label[16];

    if (redraw == 0u) {
        return;
    }

    draw_pipe_v(cx, 236u, 276u, 3u, pipe);
    (void)snprintf(label, sizeof(label), "CH%u", (unsigned)(channel + 1u));
    draw_label_box(x, 210u, 38u, 28u, card_color, UI_STROKE, label);

    (void)snprintf(label, sizeof(label), "P%u", (unsigned)(channel + 7u));
    draw_label_box(x, 276u, 38u, 24u, pressure_color, UI_WARN, label);
    draw_pipe_v(cx, 300u, 326u, 3u, pipe);
    draw_valve(x, 326u, valve);
    draw_pipe_v(cx, 350u, 396u, 3u, pipe);

    s_cache.valve_open[valve] = open;
    s_cache.channel_fill[channel] = card_color;
    s_cache.channel_sensor_fill[channel] = pressure_color;
}

static void draw_topology(uint8_t force)
{
    if (force != 0u) {
        draw_topology_static();
    }

    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        draw_tank_topology(i, force);
    }

    for (uint8_t ch = 0u; ch < APP_PCBA_CHANNEL_COUNT; ++ch) {
        draw_pcba_topology(ch, force);
    }
}

static void draw_tank_summary(uint8_t force)
{
    char line1[18];
    char line2[22];

    for (uint8_t i = 0u; i < APP_TANK_COUNT; ++i) {
        const TankDisplayItem *tank = &s_tanks[i];
        const uint16_t x = (uint16_t)(RIGHT_X + 24u + (i * 148u));
        const uint32_t now = AppPressure_Get001mmHg(tank->sensor);
        const uint32_t now_whole = pressure_whole(now);
        const uint8_t valid = AppPressure_IsValid(tank->sensor) ? 1u : 0u;
        const uint32_t fill = tank_fill(tank);

        if (force == 0u &&
            s_cache.tank_pressure[i] == now_whole &&
            s_cache.tank_pressure_valid[i] == valid &&
            s_cache.tank_summary_fill[i] == fill) {
            continue;
        }

        (void)snprintf(line1, sizeof(line1), "%s target %lu", tank->name, (unsigned long)pressure_whole(tank->target));
        (void)snprintf(line2,
                       sizeof(line2),
                       "%s%3lu mmHg",
                       valid ? "" : "?",
                       (unsigned long)now_whole);
        draw_two_line_box(x, 436u, 136u, 44u, fill, UI_STROKE, line1, line2);

        s_cache.tank_pressure[i] = now_whole;
        s_cache.tank_pressure_valid[i] = valid;
        s_cache.tank_summary_fill[i] = fill;
    }
}

static void draw_pcba_summary(uint8_t force)
{
    char line1[16];
    char line2[16];
    char standby_ua[16];

    for (uint8_t ch = 0u; ch < APP_PCBA_CHANNEL_COUNT; ++ch) {
        const uint16_t x = (uint16_t)(RIGHT_X + 24u + (ch * 110u));
        const uint32_t pressure = AppPressure_Get001mmHg(s_pcba[ch].sensor);
        const uint32_t pressure_value = pressure_whole(pressure);
        const uint32_t standby_value = current_ua_x100(AppCurrent_GetStandbyUa((uint8_t)(ch + 1u)));
        const uint32_t fill = channel_fill(ch);

        if (force == 0u &&
            s_cache.channel_pressure[ch] == pressure_value &&
            s_cache.channel_standby_x100[ch] == standby_value &&
            s_cache.channel_summary_fill[ch] == fill) {
            continue;
        }

        format_current_x100(standby_ua, sizeof(standby_ua), standby_value);
        (void)snprintf(line1, sizeof(line1), "CH%u P%lu", (unsigned)(ch + 1u), (unsigned long)pressure_value);
        (void)snprintf(line2, sizeof(line2), "S%s", standby_ua);
        draw_two_line_box(x, 528u, 100u, 44u, fill, UI_STROKE, line1, line2);

        s_cache.channel_pressure[ch] = pressure_value;
        s_cache.channel_standby_x100[ch] = standby_value;
        s_cache.channel_summary_fill[ch] = fill;
    }
}

static void draw_dynamic(uint8_t force)
{
    draw_flow_column(force);
    draw_status_bar(force);
    draw_topology(force);
    draw_tank_summary(force);
    draw_pcba_summary(force);
}

void AppDisplay_Init(AppBootMode mode)
{
    s_enabled = (mode == APP_MODE_NORMAL) ? 1u : 0u;
    s_last_refresh = 0u;
    s_last_flow_step = -1;
    display_cache_invalidate();
    if (s_enabled != 0u) {
        draw_static_shell();
        draw_dynamic(1u);
    }
}

void AppDisplay_Task(void)
{
    if (s_enabled == 0u) {
        return;
    }

    if ((HAL_GetTick() - s_last_refresh) < DISPLAY_POLL_MS) {
        return;
    }
    s_last_refresh = HAL_GetTick();

    draw_dynamic(0u);
}
