#ifndef APP_PRESSURE_CALIBRATION_H
#define APP_PRESSURE_CALIBRATION_H

#include <stdint.h>

#include "app_config.h"

#define APP_PRESSURE_CAL_ANCHOR_COUNT 4u

typedef struct {
    uint32_t raw[APP_PRESSURE_CAL_ANCHOR_COUNT];
    uint32_t pressure_001mmhg[APP_PRESSURE_CAL_ANCHOR_COUNT];
} AppPressureCalibrationProfile;

int AppPressureCalibration_ValidateProfile(const AppPressureCalibrationProfile *profile);
uint32_t AppPressureCalibration_ConvertProfile(const AppPressureCalibrationProfile *profile,
                                               uint32_t raw);

void AppPressureCalibration_Init(void);
uint16_t AppPressureCalibration_GetMask(void);
uint8_t AppPressureCalibration_IsStorageLoaded(void);
uint8_t AppPressureCalibration_HasStorageFault(void);
uint32_t AppPressureCalibration_GetStorageSequence(void);
int AppPressureCalibration_Convert(uint8_t slot,
                                   uint32_t raw,
                                   uint32_t *pressure_001mmhg);
int AppPressureCalibration_GetProfile(uint8_t slot,
                                      AppPressureCalibrationProfile *profile);
int AppPressureCalibration_SaveProfile(uint8_t slot,
                                       const AppPressureCalibrationProfile *profile);
int AppPressureCalibration_ClearProfile(uint8_t slot);

#endif
