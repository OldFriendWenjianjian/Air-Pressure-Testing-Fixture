#ifndef BSP_INTERNAL_FLASH_H
#define BSP_INTERNAL_FLASH_H

#include <stdint.h>

int BspInternalFlash_ErasePage(uint32_t page_address);
int BspInternalFlash_ProgramHalfwords(uint32_t address, const uint8_t *data, uint32_t length);
int BspInternalFlash_ProgramHalfword(uint32_t address, uint16_t value);

#endif
