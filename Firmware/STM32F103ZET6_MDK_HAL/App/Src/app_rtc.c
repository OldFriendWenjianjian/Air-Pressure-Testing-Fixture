#include "app_rtc.h"
#include "main.h"

#define APP_RTC_BACKUP_MAGIC       0x5254u
#define APP_RTC_LSE_TIMEOUT_MS     1500u
#define APP_RTC_SYNC_TIMEOUT_MS    100u

#define PWR_CR_DBP                 (1UL << 8)
#define RCC_APB1ENR_BKPEN          (1UL << 27)
#define RCC_BDCR_LSEON             (1UL << 0)
#define RCC_BDCR_LSERDY            (1UL << 1)
#define RCC_BDCR_RTCSEL_MASK       (3UL << 8)
#define RCC_BDCR_RTCSEL_LSE        (1UL << 8)
#define RCC_BDCR_RTCEN             (1UL << 15)
#define RCC_BDCR_BDRST             (1UL << 16)
#define RTC_CRL_RSF                ((uint16_t)0x0008u)
#define RTC_CRL_CNF                ((uint16_t)0x0010u)
#define RTC_CRL_RTOFF              ((uint16_t)0x0020u)

static uint8_t s_flags;

static void enable_backup_domain_write(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    RCC->APB1ENR |= RCC_APB1ENR_BKPEN;
    PWR->CR |= PWR_CR_DBP;
}

static uint8_t wait_rtc_ready(uint32_t timeout_ms)
{
    const uint32_t start = HAL_GetTick();
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0u) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return 0u;
        }
    }
    return 1u;
}

static void rtc_write_counter(uint32_t value)
{
    if (wait_rtc_ready(APP_RTC_SYNC_TIMEOUT_MS) == 0u) {
        return;
    }
    RTC->CRL |= RTC_CRL_CNF;
    RTC->CNTH = (uint16_t)(value >> 16);
    RTC->CNTL = (uint16_t)(value & 0xFFFFu);
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    (void)wait_rtc_ready(APP_RTC_SYNC_TIMEOUT_MS);
}

static void rtc_write_prescaler_1hz(void)
{
    if (wait_rtc_ready(APP_RTC_SYNC_TIMEOUT_MS) == 0u) {
        return;
    }
    RTC->CRL |= RTC_CRL_CNF;
    RTC->PRLH = 0u;
    RTC->PRLL = 32767u;
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    (void)wait_rtc_ready(APP_RTC_SYNC_TIMEOUT_MS);
}

static uint8_t wait_lse_ready(void)
{
    const uint32_t start = HAL_GetTick();

    RCC->BDCR |= RCC_BDCR_LSEON;
    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
        if ((HAL_GetTick() - start) >= APP_RTC_LSE_TIMEOUT_MS) {
            return 0u;
        }
    }
    return 1u;
}

void AppRtc_Init(void)
{
    uint8_t backup_valid;
    uint8_t lse_ready;

    enable_backup_domain_write();
    backup_valid = (BKP->DR1 == APP_RTC_BACKUP_MAGIC) ? 1u : 0u;
    lse_ready = wait_lse_ready();

    if (lse_ready != 0u && (RCC->BDCR & RCC_BDCR_RTCEN) == 0u) {
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL_MASK) | RCC_BDCR_RTCSEL_LSE;
        RCC->BDCR |= RCC_BDCR_RTCEN;
        RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
        (void)wait_rtc_ready(APP_RTC_SYNC_TIMEOUT_MS);
        rtc_write_prescaler_1hz();
    }

    if (lse_ready != 0u) {
        BKP->DR1 = APP_RTC_BACKUP_MAGIC;
    }

    s_flags = 0u;
    if (lse_ready != 0u) {
        s_flags |= APP_RTC_FLAG_INITIALIZED | APP_RTC_FLAG_OSCILLATOR_READY;
    }
    if (backup_valid != 0u) {
        s_flags |= APP_RTC_FLAG_BACKUP_VALID;
    }
}

uint32_t AppRtc_GetEpochSeconds(void)
{
    uint16_t high1;
    uint16_t low;
    uint16_t high2;

    high1 = RTC->CNTH;
    low = RTC->CNTL;
    high2 = RTC->CNTH;
    if (high1 != high2) {
        low = RTC->CNTL;
    }
    return ((uint32_t)high2 << 16) | (uint32_t)low;
}

uint8_t AppRtc_GetFlags(void)
{
    if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0u) {
        s_flags |= APP_RTC_FLAG_OSCILLATOR_READY | APP_RTC_FLAG_INITIALIZED;
    } else {
        s_flags &= (uint8_t)~APP_RTC_FLAG_OSCILLATOR_READY;
    }
    if (BKP->DR1 == APP_RTC_BACKUP_MAGIC) {
        s_flags |= APP_RTC_FLAG_BACKUP_VALID;
    } else {
        s_flags &= (uint8_t)~APP_RTC_FLAG_BACKUP_VALID;
    }
    return s_flags;
}

int AppRtc_SetEpochSeconds(uint32_t epoch_seconds)
{
    enable_backup_domain_write();
    if ((AppRtc_GetFlags() & APP_RTC_FLAG_OSCILLATOR_READY) == 0u) {
        return -1;
    }

    rtc_write_counter(epoch_seconds);
    BKP->DR1 = APP_RTC_BACKUP_MAGIC;
    s_flags |= APP_RTC_FLAG_INITIALIZED | APP_RTC_FLAG_BACKUP_VALID;
    return 0;
}
