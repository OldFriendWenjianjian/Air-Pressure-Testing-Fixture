#include "storage_w25q_msc.h"
#include "app_config.h"
#include "bsp_w25q128.h"

int8_t STORAGE_Init_FS(uint8_t lun)
{
    (void)lun;
    return W25Q128_Init() == 0 ? 0 : -1;
}

int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    if (block_num == 0 || block_size == 0) {
        return -1;
    }

    *block_num = MSC_BLOCK_COUNT;
    *block_size = MSC_BLOCK_BYTES;
    return 0;
}

int8_t STORAGE_IsReady_FS(uint8_t lun)
{
    (void)lun;
    return W25Q128_IsReady() ? 0 : -1;
}

int8_t STORAGE_IsWriteProtected_FS(uint8_t lun)
{
    (void)lun;
    return 0;
}

int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return W25Q128_Read(blk_addr * MSC_BLOCK_BYTES, buf, (uint32_t)blk_len * MSC_BLOCK_BYTES) == 0 ? 0 : -1;
}

int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return W25Q128_Write(blk_addr * MSC_BLOCK_BYTES, buf, (uint32_t)blk_len * MSC_BLOCK_BYTES) == 0 ? 0 : -1;
}

int8_t STORAGE_GetMaxLun_FS(void)
{
    return 0;
}
