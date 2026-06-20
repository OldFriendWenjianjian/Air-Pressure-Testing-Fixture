#ifndef APP_ADC_CALIBRATION_H
#define APP_ADC_CALIBRATION_H

#include <stdint.h>

#define APP_ADC_CAL_FLAGS_VALID             0x01u
#define APP_ADC_CAL_FLAGS_RANGE_ERROR       0x02u

void AppAdcCalibration_Init(void);
int AppAdcCalibration_Refresh(void);
uint32_t AppAdcCalibration_GetVddaMv(void);
uint32_t AppAdcCalibration_GetScalePpm(void);
uint16_t AppAdcCalibration_GetVrefintRaw(void);
uint8_t AppAdcCalibration_GetFlags(void);

#endif
