#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdint.h>

void AppPower_AllOff(void);
void AppPower_Enable5V(void);
void AppPower_Enable45V(void);
void AppPower_Enable50mATestCircuit(int enable);
uint8_t AppPower_Is5VEnabled(void);
uint8_t AppPower_Is45VEnabled(void);
uint8_t AppPower_Is50mATestCircuitEnabled(void);

#endif
