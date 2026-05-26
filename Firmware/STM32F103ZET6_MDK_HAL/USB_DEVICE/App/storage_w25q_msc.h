#ifndef STORAGE_W25Q_MSC_H
#define STORAGE_W25Q_MSC_H

#include <stdint.h>

int8_t STORAGE_Init_FS(uint8_t lun);
int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
int8_t STORAGE_IsReady_FS(uint8_t lun);
int8_t STORAGE_IsWriteProtected_FS(uint8_t lun);
int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
int8_t STORAGE_GetMaxLun_FS(void);

#endif
