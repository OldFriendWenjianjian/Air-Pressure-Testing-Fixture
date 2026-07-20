#include "bsp_internal_flash.h"
#include "app_config.h"
#include "main.h"

#define FLASH_KEY1_VALUE              0x45670123u
#define FLASH_KEY2_VALUE              0xCDEF89ABu
#define FLASH_SR_BSY_BIT              (1u << 0)
#define FLASH_SR_PGERR_BIT            (1u << 2)
#define FLASH_SR_WRPRTERR_BIT         (1u << 4)
#define FLASH_SR_EOP_BIT              (1u << 5)
#define FLASH_CR_PG_BIT               (1u << 0)
#define FLASH_CR_PER_BIT              (1u << 1)
#define FLASH_CR_STRT_BIT             (1u << 6)
#define FLASH_CR_LOCK_BIT             (1u << 7)
#define FLASH_OPERATION_TIMEOUT       7200000u

static int wait_ready(void)
{
    uint32_t timeout = FLASH_OPERATION_TIMEOUT;

    while ((FLASH->SR & FLASH_SR_BSY_BIT) != 0u) {
        if (timeout-- == 0u) {
            return -1;
        }
    }
    if ((FLASH->SR & (FLASH_SR_PGERR_BIT | FLASH_SR_WRPRTERR_BIT)) != 0u) {
        return -1;
    }
    return 0;
}

static int unlock(void)
{
    uint32_t timeout = FLASH_OPERATION_TIMEOUT;

    while ((FLASH->SR & FLASH_SR_BSY_BIT) != 0u) {
        if (timeout-- == 0u) {
            return -1;
        }
    }
    FLASH->SR = FLASH_SR_EOP_BIT | FLASH_SR_PGERR_BIT | FLASH_SR_WRPRTERR_BIT;
    if ((FLASH->CR & FLASH_CR_LOCK_BIT) != 0u) {
        FLASH->KEYR = FLASH_KEY1_VALUE;
        FLASH->KEYR = FLASH_KEY2_VALUE;
    }
    return (FLASH->CR & FLASH_CR_LOCK_BIT) == 0u ? 0 : -1;
}

static void clear_status(void)
{
    FLASH->SR = FLASH_SR_EOP_BIT | FLASH_SR_PGERR_BIT | FLASH_SR_WRPRTERR_BIT;
}

static void lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK_BIT;
}

int BspInternalFlash_ErasePage(uint32_t page_address)
{
    int result;

    if ((page_address & (APP_PRESSURE_CAL_FLASH_PAGE_BYTES - 1u)) != 0u ||
        (page_address != APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS &&
         page_address != APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS)) {
        return -1;
    }
    if (unlock() != 0) {
        return -1;
    }

    clear_status();
    FLASH->CR |= FLASH_CR_PER_BIT;
    FLASH->AR = page_address;
    FLASH->CR |= FLASH_CR_STRT_BIT;
    result = wait_ready();
    FLASH->CR &= ~FLASH_CR_PER_BIT;
    if (result == 0 && *(const volatile uint32_t *)page_address != 0xFFFFFFFFu) {
        result = -1;
    }
    clear_status();
    lock();
    return result;
}

int BspInternalFlash_ProgramHalfword(uint32_t address, uint16_t value)
{
    int result;

    if ((address & 1u) != 0u || address < APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS ||
        address >= (APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS + APP_PRESSURE_CAL_FLASH_PAGE_BYTES)) {
        return -1;
    }
    if (unlock() != 0) {
        return -1;
    }

    clear_status();
    FLASH->CR |= FLASH_CR_PG_BIT;
    *(volatile uint16_t *)address = value;
    result = wait_ready();
    FLASH->CR &= ~FLASH_CR_PG_BIT;
    if (result == 0 && *(const volatile uint16_t *)address != value) {
        result = -1;
    }
    clear_status();
    lock();
    return result;
}

int BspInternalFlash_ProgramHalfwords(uint32_t address, const uint8_t *data, uint32_t length)
{
    int result = 0;

    if (data == 0 || length == 0u || length > (APP_PRESSURE_CAL_FLASH_PAGE_BYTES * 2u) ||
        (address & 1u) != 0u || (length & 1u) != 0u ||
        address < APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS ||
        address > (APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS +
                   APP_PRESSURE_CAL_FLASH_PAGE_BYTES - length)) {
        return -1;
    }
    if (unlock() != 0) {
        return -1;
    }

    for (uint32_t offset = 0u; offset < length; offset += 2u) {
        const uint16_t value = (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8);

        clear_status();
        FLASH->CR |= FLASH_CR_PG_BIT;
        *(volatile uint16_t *)(address + offset) = value;
        result = wait_ready();
        FLASH->CR &= ~FLASH_CR_PG_BIT;
        if (result != 0 || *(const volatile uint16_t *)(address + offset) != value) {
            result = -1;
            break;
        }
    }

    clear_status();
    lock();
    return result;
}
