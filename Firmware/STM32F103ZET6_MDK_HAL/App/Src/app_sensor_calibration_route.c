#include "app_sensor_calibration.h"

int AppSensorCalibration_ResolveRoute(uint8_t in_place_mode,
                                      uint8_t destination_slot,
                                      AppSensorCalibrationRoute *route)
{
    static const uint8_t tank_inlet_valves[APP_TANK_COUNT] = {1u, 3u, 5u, 7u, 9u, 11u};
    static const uint8_t tank_outlet_valves[APP_TANK_COUNT] = {2u, 4u, 6u, 8u, 10u, 12u};
    static const uint8_t tank_relief_valves[APP_TANK_COUNT] = {21u, 22u, 23u, 24u, 25u, 26u};

    if (route == 0 || in_place_mode > 1u || destination_slot >= APP_PRESSURE_SENSOR_COUNT) {
        return -1;
    }

    route->destination_slot = destination_slot;
    route->in_place_mode = in_place_mode;
    route->source_index = in_place_mode != 0u ? destination_slot : 0u;
    route->scan_mask = (uint16_t)1u << route->source_index;
    route->channel_valve = 0u;

    if (route->source_index < APP_TANK_COUNT) {
        route->inlet_valve = tank_inlet_valves[route->source_index];
        route->outlet_valve = tank_outlet_valves[route->source_index];
        route->relief_valve = tank_relief_valves[route->source_index];
    } else {
        route->scan_mask |= 0x0001u;
        route->inlet_valve = 1u;
        route->outlet_valve = 2u;
        route->relief_valve = 21u;
        route->channel_valve = (uint8_t)(13u + route->source_index - APP_TANK_COUNT);
    }
    return 0;
}
