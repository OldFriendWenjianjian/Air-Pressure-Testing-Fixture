#include "app_adc_calibration.h"
#include "app_config.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

#define APP_ADC_REF_VREFINT_CHANNEL         ADC_CHANNEL_17
#define APP_ADC_REF_VREFINT_TYP_MV          1200u
#define APP_ADC_REF_SAMPLE_COUNT            8u
#define APP_ADC_REF_MIN_VDDA_MV             2500u
#define APP_ADC_REF_MAX_VDDA_MV             3600u

#define ADC_CR2_TSVREFE                     (1UL << 23)

static uint16_t s_vrefint_raw;
static uint32_t s_vdda_mv = APP_CURRENT_ADC_VREF_MV;
static uint32_t s_scale_ppm = 1000000u;
static uint8_t s_flags;

static uint16_t read_adc_raw(uint32_t adc_channel, uint32_t sampling_time)
{
    ADC_ChannelConfTypeDef channel = {0};
    uint16_t raw = 0u;

    channel.Channel = adc_channel;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = sampling_time;

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

static uint16_t sample_vrefint(void)
{
    uint32_t sum = 0u;

    ADC1->CR2 |= ADC_CR2_TSVREFE;
    HAL_Delay(1u);
    (void)read_adc_raw(APP_ADC_REF_VREFINT_CHANNEL, ADC_SAMPLETIME_239CYCLES_5);

    for (uint32_t i = 0u; i < APP_ADC_REF_SAMPLE_COUNT; ++i) {
        sum += read_adc_raw(APP_ADC_REF_VREFINT_CHANNEL, ADC_SAMPLETIME_239CYCLES_5);
    }

    return (uint16_t)((sum + (APP_ADC_REF_SAMPLE_COUNT / 2u)) / APP_ADC_REF_SAMPLE_COUNT);
}

static uint32_t vdda_from_vrefint_raw(uint16_t raw)
{
    if (raw == 0u) {
        return APP_CURRENT_ADC_VREF_MV;
    }

    return (uint32_t)((((uint64_t)APP_ADC_REF_VREFINT_TYP_MV * 4095u) + (raw / 2u)) / raw);
}

void AppAdcCalibration_Init(void)
{
    s_vrefint_raw = 0u;
    s_vdda_mv = APP_CURRENT_ADC_VREF_MV;
    s_scale_ppm = 1000000u;
    s_flags = 0u;
    (void)AppAdcCalibration_Refresh();
}

int AppAdcCalibration_Refresh(void)
{
    const uint16_t raw = sample_vrefint();
    const uint32_t vdda_mv = vdda_from_vrefint_raw(raw);

    s_vrefint_raw = raw;
    if (raw == 0u ||
        vdda_mv < APP_ADC_REF_MIN_VDDA_MV ||
        vdda_mv > APP_ADC_REF_MAX_VDDA_MV) {
        s_flags = APP_ADC_CAL_FLAGS_RANGE_ERROR;
        return -1;
    }

    s_vdda_mv = vdda_mv;
    s_scale_ppm = (uint32_t)((((uint64_t)vdda_mv * 1000000u) + (APP_CURRENT_ADC_VREF_MV / 2u)) /
                             APP_CURRENT_ADC_VREF_MV);
    s_flags = APP_ADC_CAL_FLAGS_VALID;
    return 0;
}

uint32_t AppAdcCalibration_GetVddaMv(void)
{
    return s_vdda_mv;
}

uint32_t AppAdcCalibration_GetScalePpm(void)
{
    return s_scale_ppm;
}

uint16_t AppAdcCalibration_GetVrefintRaw(void)
{
    return s_vrefint_raw;
}

uint8_t AppAdcCalibration_GetFlags(void)
{
    return s_flags;
}
