#ifndef BSP_W25Q128_H
#define BSP_W25Q128_H

#include <stdint.h>

int W25Q128_Init(void);
uint32_t W25Q128_ReadJedecId(void);
int W25Q128_Read(uint32_t address, uint8_t *data, uint32_t len);
int W25Q128_PageProgram(uint32_t address, const uint8_t *data, uint32_t len);
int W25Q128_SectorErase(uint32_t address);
int W25Q128_Write(uint32_t address, const uint8_t *data, uint32_t len);
int W25Q128_IsReady(void);

#endif
