#include "app_pressure_calibration_store_logic.h"

#include "app_config.h"

uint32_t AppPressureCalibration_SelectInactivePage(uint32_t active_page)
{
    if (active_page == APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS) {
        return APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS;
    }
    return APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS;
}

uint8_t AppPressureCalibration_ClearNeedsPersist(uint16_t calibrated_mask,
                                                 uint8_t slot,
                                                 uint8_t storage_fault)
{
    if (slot >= APP_PRESSURE_SENSOR_COUNT) {
        return 0u;
    }
    return ((calibrated_mask & ((uint16_t)1u << slot)) != 0u || storage_fault != 0u) ?
           1u : 0u;
}
