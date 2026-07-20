#ifndef APP_SENSOR_CALIBRATION_H
#define APP_SENSOR_CALIBRATION_H

#include <stdint.h>

#include "app_pressure_calibration.h"

typedef enum {
    APP_SENSOR_CAL_ACTUATOR_STOP = 0,
    APP_SENSOR_CAL_ACTUATOR_FILL = 1,
    APP_SENSOR_CAL_ACTUATOR_RELEASE = 2,
    APP_SENSOR_CAL_ACTUATOR_AUTO_VENT = 3
} AppSensorCalibrationActuator;

typedef enum {
    APP_SENSOR_CAL_DETAIL_OK = 0,
    APP_SENSOR_CAL_DETAIL_BAD_STATE = 1,
    APP_SENSOR_CAL_DETAIL_BAD_VALUE = 2,
    APP_SENSOR_CAL_DETAIL_SOURCE_INVALID = 3,
    APP_SENSOR_CAL_DETAIL_LEASE_EXPIRED = 4,
    APP_SENSOR_CAL_DETAIL_OVERPRESSURE = 5,
    APP_SENSOR_CAL_DETAIL_VENT_COMPLETE = 6,
    APP_SENSOR_CAL_DETAIL_VENT_TIMEOUT = 7,
    APP_SENSOR_CAL_DETAIL_STORAGE_ERROR = 8,
    APP_SENSOR_CAL_DETAIL_PROFILE_INVALID = 9,
    APP_SENSOR_CAL_DETAIL_ZERO_VALUE_INVALID = 10,
    APP_SENSOR_CAL_DETAIL_CAPTURED = 11,
    APP_SENSOR_CAL_DETAIL_SAVED = 12,
    APP_SENSOR_CAL_DETAIL_CLEARED = 13,
    APP_SENSOR_CAL_DETAIL_NOT_ENOUGH_SAMPLES = 14
} AppSensorCalibrationDetail;

typedef struct {
    uint16_t scan_mask;
    uint8_t source_index;
    uint8_t destination_slot;
    uint8_t in_place_mode;
    uint8_t inlet_valve;
    uint8_t outlet_valve;
    uint8_t relief_valve;
    uint8_t channel_valve;
} AppSensorCalibrationRoute;

typedef enum {
    APP_SENSOR_CAL_REQUEST_OK = 0,
    APP_SENSOR_CAL_REQUEST_BAD_LENGTH = 1,
    APP_SENSOR_CAL_REQUEST_BAD_VALUE = 2
} AppSensorCalibrationRequestResult;

typedef struct {
    uint8_t operation;
    uint8_t mode;
    uint8_t slot;
    uint8_t actuator;
    uint8_t point_index;
    uint16_t lease_ms;
    uint32_t actual_001mmhg;
} AppSensorCalibrationRequest;

typedef struct {
    uint8_t flags;
    uint8_t actuator;
    uint8_t captured_mask;
    uint16_t calibrated_mask;
    uint8_t selected_slot;
    uint8_t in_place_mode;
    uint8_t last_detail;
    uint32_t live_raw_average;
    uint32_t live_nominal_001mmhg;
    AppPressureCalibrationProfile profile;
} AppSensorCalibrationStatus;

void AppSensorCalibration_Init(void);
int AppSensorCalibration_ResolveRoute(uint8_t in_place_mode,
                                      uint8_t destination_slot,
                                      AppSensorCalibrationRoute *route);
AppSensorCalibrationRequestResult AppSensorCalibration_DecodeRequest(
    const uint8_t *payload,
    uint16_t length,
    AppSensorCalibrationRequest *request);
int AppSensorCalibration_Enter(uint8_t in_place_mode, uint8_t destination_slot);
void AppSensorCalibration_Exit(void);
void AppSensorCalibration_Task(void);
int AppSensorCalibration_Jog(uint8_t actuator, uint16_t lease_ms);
int AppSensorCalibration_StartAutoVent(void);
int AppSensorCalibration_CancelAutoVent(void);
int AppSensorCalibration_Record(uint8_t point_index, uint32_t actual_001mmhg);
int AppSensorCalibration_SaveSlot(uint8_t slot);
int AppSensorCalibration_ClearSlot(uint8_t slot);
int AppSensorCalibration_ResetSession(void);
void AppSensorCalibration_GetStatus(uint8_t selected_slot,
                                    AppSensorCalibrationStatus *status);
uint8_t AppSensorCalibration_IsActive(void);
uint8_t AppSensorCalibration_GetActuator(void);

#endif
