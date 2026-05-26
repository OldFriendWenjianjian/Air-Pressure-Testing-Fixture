#ifndef LT768_PORT_H
#define LT768_PORT_H

#include <stdint.h>

int LT768_PortInit(void);
void LT768_Reset(void);
void LT768_WriteCommand(uint8_t cmd);
void LT768_WriteData(uint8_t data);
uint8_t LT768_ReadStatus(void);
uint8_t LT768_ReadData(void);

#endif
