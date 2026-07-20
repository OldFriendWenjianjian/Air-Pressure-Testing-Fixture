#include "app_sensor_calibration.h"

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

AppSensorCalibrationRequestResult AppSensorCalibration_DecodeRequest(
    const uint8_t *payload,
    uint16_t length,
    AppSensorCalibrationRequest *request)
{
    uint16_t expected_length;

    if (payload == 0 || request == 0 || length == 0u) {
        return APP_SENSOR_CAL_REQUEST_BAD_LENGTH;
    }
    switch (payload[0]) {
    case 1u:
        expected_length = 3u;
        break;
    case 2u:
    case 4u:
    case 5u:
    case 9u:
        expected_length = 1u;
        break;
    case 3u:
        expected_length = 4u;
        break;
    case 6u:
        expected_length = 6u;
        break;
    case 7u:
    case 8u:
        expected_length = 2u;
        break;
    default:
        return APP_SENSOR_CAL_REQUEST_BAD_VALUE;
    }
    if (length != expected_length) {
        return APP_SENSOR_CAL_REQUEST_BAD_LENGTH;
    }

    request->operation = payload[0];
    request->mode = 0u;
    request->slot = 0u;
    request->actuator = 0u;
    request->point_index = 0u;
    request->lease_ms = 0u;
    request->actual_001mmhg = 0u;

    if (payload[0] == 1u) {
        if (payload[1] > 1u || payload[2] < 1u ||
            payload[2] > APP_PRESSURE_SENSOR_COUNT) {
            return APP_SENSOR_CAL_REQUEST_BAD_VALUE;
        }
        request->mode = payload[1];
        request->slot = payload[2];
    } else if (payload[0] == 3u) {
        request->actuator = payload[1];
        request->lease_ms = get_u16_le(&payload[2]);
        if (request->actuator > APP_SENSOR_CAL_ACTUATOR_RELEASE ||
            (request->actuator != APP_SENSOR_CAL_ACTUATOR_STOP && request->lease_ms == 0u)) {
            return APP_SENSOR_CAL_REQUEST_BAD_VALUE;
        }
    } else if (payload[0] == 6u) {
        request->point_index = payload[1];
        request->actual_001mmhg = get_u32_le(&payload[2]);
        if (request->point_index >= APP_PRESSURE_CAL_ANCHOR_COUNT) {
            return APP_SENSOR_CAL_REQUEST_BAD_VALUE;
        }
    } else if (payload[0] == 7u || payload[0] == 8u) {
        if (payload[1] < 1u || payload[1] > APP_PRESSURE_SENSOR_COUNT) {
            return APP_SENSOR_CAL_REQUEST_BAD_VALUE;
        }
        request->slot = payload[1];
    }
    return APP_SENSOR_CAL_REQUEST_OK;
}
