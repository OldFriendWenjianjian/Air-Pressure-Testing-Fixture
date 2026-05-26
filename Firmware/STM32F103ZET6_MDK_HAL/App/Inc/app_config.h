#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_PCBA_CHANNEL_COUNT              8u
#define APP_PRESSURE_SENSOR_COUNT           14u
#define APP_TANK_COUNT                      6u

#define APP_PCBA_UART_BAUDRATE              115200u
#define APP_KEY1_HOLD_TO_MSC_MS             1500u

#define APP_PRESSURE_SCALE_PER_MMHG         1000u
#define APP_PRESSURE_50_MMHG                (50u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PRESSURE_100_MMHG               (100u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PRESSURE_150_MMHG               (150u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PRESSURE_200_MMHG               (200u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PRESSURE_250_MMHG               (250u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PRESSURE_300_MMHG               (300u * APP_PRESSURE_SCALE_PER_MMHG)

#define APP_PRESSURE_STABLE_WINDOW_RAW      20u
#define APP_PRESSURE_STABLE_TIME_MS         1000u
#define APP_AIRTIGHTNESS_HOLD_TIME_MS       5000u
#define APP_AIRTIGHTNESS_MAX_DROP_001MMHG   (5u * APP_PRESSURE_SCALE_PER_MMHG)
#define APP_PCBA_RESPONSE_TIMEOUT_MS        200u
#define APP_STAGE_SETTLE_TIMEOUT_MS         30000u
#define APP_RESULT_CONFIRM_KEY_DEBOUNCE_MS  50u

#define W25Q128_TOTAL_BYTES                 (16u * 1024u * 1024u)
#define W25Q128_SECTOR_BYTES                4096u
#define W25Q128_PAGE_BYTES                  256u
#define MSC_BLOCK_BYTES                     512u
#define MSC_BLOCK_COUNT                     (W25Q128_TOTAL_BYTES / MSC_BLOCK_BYTES)

#endif
