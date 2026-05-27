#include "app_valves.h"
#include "board_pins.h"

static uint8_t s_valve_open[26];

void AppValves_AllClosed(void)
{
    for (uint8_t i = 1u; i <= 26u; ++i) {
        AppValves_Set(i, 0u);
    }
}

void AppValves_Set(uint8_t valve_number, uint8_t open)
{
    if (valve_number == 0u || valve_number > 26u) {
        return;
    }

    const BoardGpio *pin = &BOARD_VALVE_PINS[valve_number - 1u];
    HAL_GPIO_WritePin(pin->port, pin->pin, open ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_valve_open[valve_number - 1u] = open ? 1u : 0u;
}

void AppValves_OpenMask(const uint8_t *valves, uint8_t count)
{
    if (valves == 0) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        AppValves_Set(valves[i], 1u);
    }
}

void AppValves_CloseMask(const uint8_t *valves, uint8_t count)
{
    if (valves == 0) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        AppValves_Set(valves[i], 0u);
    }
}

uint8_t AppValves_IsOpen(uint8_t valve_number)
{
    if (valve_number == 0u || valve_number > 26u) {
        return 0u;
    }
    return s_valve_open[valve_number - 1u];
}
