#include "app_current.h"
#include "app_config.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

static const uint32_t s_current_adc_channels[APP_PCBA_CHANNEL_COUNT] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
    ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
};

static uint16_t s_raw[APP_PCBA_CHANNEL_COUNT];
static uint32_t s_standby_ua[APP_PCBA_CHANNEL_COUNT];
static uint32_t s_work_ua[APP_PCBA_CHANNEL_COUNT];

static uint16_t read_adc_raw(uint32_t adc_channel)
{
    ADC_ChannelConfTypeDef channel = {0};
    uint16_t raw = 0u;

    channel.Channel = adc_channel;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
        return 0u;
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return 0u;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10u) == HAL_OK) {
        raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);

    return raw;
}

static uint32_t raw_to_adc_mv(uint16_t raw)
{
    return (((uint32_t)raw * APP_CURRENT_ADC_VREF_MV) + 2047u) / 4095u;
}

static uint32_t raw_to_current_ua(uint16_t raw, uint32_t shunt_mohm)
{
    uint32_t adc_mv = raw_to_adc_mv(raw);
    uint64_t denom = (uint64_t)shunt_mohm * APP_PCBA_CURRENT_AMP_GAIN;

    if (denom == 0u) {
        return 0u;
    }

    return (uint32_t)((((uint64_t)adc_mv * 1000000u) + (denom / 2u)) / denom);
}

void AppCurrent_Init(void)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        s_raw[i] = 0u;
        s_standby_ua[i] = 0u;
        s_work_ua[i] = 0u;
    }
}

int AppCurrent_CaptureAll(AppCurrentMode mode)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        uint16_t raw = read_adc_raw(s_current_adc_channels[i]);

        s_raw[i] = raw;
        if (mode == APP_CURRENT_MODE_STANDBY) {
            s_standby_ua[i] = raw_to_current_ua(raw, APP_PCBA_STANDBY_SHUNT_MOHM);
        } else {
            s_work_ua[i] = raw_to_current_ua(raw, APP_PCBA_WORK_SHUNT_MOHM);
        }
    }

    return 0;
}

uint16_t AppCurrent_GetRaw(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_raw[channel - 1u];
}

uint32_t AppCurrent_GetStandbyUa(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_standby_ua[channel - 1u];
}

uint32_t AppCurrent_GetWorkUa(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_work_ua[channel - 1u];
}

int AppCurrent_StandbyAllInRange(void)
{
#if APP_PCBA_STANDBY_CURRENT_MAX_UA > 0
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        if (s_standby_ua[i] > APP_PCBA_STANDBY_CURRENT_MAX_UA) {
            return 0;
        }
    }
#endif
    return 1;
}

int AppCurrent_WorkAllInRange(void)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
#if APP_PCBA_WORK_CURRENT_MIN_UA > 0
        if (s_work_ua[i] < APP_PCBA_WORK_CURRENT_MIN_UA) {
            return 0;
        }
#endif
#if APP_PCBA_WORK_CURRENT_MAX_UA > 0
        if (s_work_ua[i] > APP_PCBA_WORK_CURRENT_MAX_UA) {
            return 0;
        }
#endif
    }

    return 1;
}
