#ifndef APP_VALVES_H
#define APP_VALVES_H

#include <stdint.h>

void AppValves_AllClosed(void);
void AppValves_Set(uint8_t valve_number, uint8_t open);
void AppValves_OpenMask(const uint8_t *valves, uint8_t count);
void AppValves_CloseMask(const uint8_t *valves, uint8_t count);
uint8_t AppValves_IsOpen(uint8_t valve_number);

#endif
