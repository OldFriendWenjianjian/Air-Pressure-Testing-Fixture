#ifndef APP_PRESSURE_CALIBRATION_STORE_LOGIC_H
#define APP_PRESSURE_CALIBRATION_STORE_LOGIC_H

#include <stdint.h>

uint32_t AppPressureCalibration_SelectInactivePage(uint32_t active_page);
uint8_t AppPressureCalibration_ClearNeedsPersist(uint16_t calibrated_mask,
                                                 uint8_t slot,
                                                 uint8_t storage_fault);

#endif
