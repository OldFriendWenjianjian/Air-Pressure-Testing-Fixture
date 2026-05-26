#include "bsp_w25q128.h"
#include "app_config.h"
#include "board_pins.h"
#include "main.h"

extern SPI_HandleTypeDef hspi3;

#define W25Q_CMD_WRITE_ENABLE       0x06u
#define W25Q_CMD_READ_STATUS1       0x05u
#define W25Q_CMD_PAGE_PROGRAM       0x02u
#define W25Q_CMD_READ_DATA          0x03u
#define W25Q_CMD_SECTOR_ERASE       0x20u
#define W25Q_CMD_JEDEC_ID           0x9Fu

static uint8_t s_sector_cache[W25Q128_SECTOR_BYTES];

static void cs_low(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_RESET);
}

static void cs_high(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET);
}

static int tx(const uint8_t *data, uint16_t len)
{
    return HAL_SPI_Transmit(&hspi3, (uint8_t *)data, len, 1000u) == HAL_OK ? 0 : -1;
}

static int rx(uint8_t *data, uint16_t len)
{
    return HAL_SPI_Receive(&hspi3, data, len, 1000u) == HAL_OK ? 0 : -1;
}

static int write_enable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;

    cs_low();
    int ret = tx(&cmd, 1u);
    cs_high();
    return ret;
}

static int wait_busy(void)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    uint8_t status = 0x01u;
    uint32_t start = HAL_GetTick();

    while ((status & 0x01u) != 0u) {
        if ((HAL_GetTick() - start) > 5000u) {
            return -1;
        }
        cs_low();
        if (tx(&cmd, 1u) != 0 || rx(&status, 1u) != 0) {
            cs_high();
            return -1;
        }
        cs_high();
    }

    return 0;
}

static void put_addr(uint8_t *cmd, uint8_t op, uint32_t address)
{
    cmd[0] = op;
    cmd[1] = (uint8_t)((address >> 16) & 0xFFu);
    cmd[2] = (uint8_t)((address >> 8) & 0xFFu);
    cmd[3] = (uint8_t)(address & 0xFFu);
}

int W25Q128_Init(void)
{
    return W25Q128_ReadJedecId() == 0u ? -1 : 0;
}

uint32_t W25Q128_ReadJedecId(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3] = {0};

    cs_low();
    if (tx(&cmd, 1u) != 0 || rx(id, 3u) != 0) {
        cs_high();
        return 0u;
    }
    cs_high();

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

int W25Q128_Read(uint32_t address, uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];

    if (data == 0 || (address + len) > W25Q128_TOTAL_BYTES) {
        return -1;
    }

    put_addr(cmd, W25Q_CMD_READ_DATA, address);
    cs_low();
    int ret = tx(cmd, sizeof(cmd));
    if (ret == 0) {
        ret = rx(data, (uint16_t)len);
    }
    cs_high();

    return ret;
}

int W25Q128_PageProgram(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];

    if (data == 0 || len == 0u || len > W25Q128_PAGE_BYTES || (address + len) > W25Q128_TOTAL_BYTES) {
        return -1;
    }
    if (((address & (W25Q128_PAGE_BYTES - 1u)) + len) > W25Q128_PAGE_BYTES) {
        return -1;
    }

    if (wait_busy() != 0 || write_enable() != 0) {
        return -1;
    }

    put_addr(cmd, W25Q_CMD_PAGE_PROGRAM, address);
    cs_low();
    int ret = tx(cmd, sizeof(cmd));
    if (ret == 0) {
        ret = tx(data, (uint16_t)len);
    }
    cs_high();

    return ret == 0 ? wait_busy() : ret;
}

int W25Q128_SectorErase(uint32_t address)
{
    uint8_t cmd[4];
    uint32_t sector = address & ~(W25Q128_SECTOR_BYTES - 1u);

    if (sector >= W25Q128_TOTAL_BYTES) {
        return -1;
    }
    if (wait_busy() != 0 || write_enable() != 0) {
        return -1;
    }

    put_addr(cmd, W25Q_CMD_SECTOR_ERASE, sector);
    cs_low();
    int ret = tx(cmd, sizeof(cmd));
    cs_high();

    return ret == 0 ? wait_busy() : ret;
}

int W25Q128_Write(uint32_t address, const uint8_t *data, uint32_t len)
{
    if (data == 0 || (address + len) > W25Q128_TOTAL_BYTES) {
        return -1;
    }

    while (len > 0u) {
        uint32_t sector_addr = address & ~(W25Q128_SECTOR_BYTES - 1u);
        uint32_t sector_off = address - sector_addr;
        uint32_t chunk = W25Q128_SECTOR_BYTES - sector_off;
        if (chunk > len) {
            chunk = len;
        }

        if (W25Q128_Read(sector_addr, s_sector_cache, W25Q128_SECTOR_BYTES) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            s_sector_cache[sector_off + i] = data[i];
        }
        if (W25Q128_SectorErase(sector_addr) != 0) {
            return -1;
        }

        uint32_t written = 0u;
        while (written < W25Q128_SECTOR_BYTES) {
            if (W25Q128_PageProgram(sector_addr + written,
                                    &s_sector_cache[written],
                                    W25Q128_PAGE_BYTES) != 0) {
                return -1;
            }
            written += W25Q128_PAGE_BYTES;
        }

        address += chunk;
        data += chunk;
        len -= chunk;
    }

    return 0;
}

int W25Q128_IsReady(void)
{
    return wait_busy() == 0;
}
