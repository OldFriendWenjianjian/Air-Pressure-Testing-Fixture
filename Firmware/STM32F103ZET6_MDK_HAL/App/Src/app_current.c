#include "app_current.h"
#include "app_adc_calibration.h"
#include "app_config.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

static const uint32_t s_current_adc_channels[APP_PCBA_CHANNEL_COUNT] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
    ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
};

static uint16_t s_raw[APP_PCBA_CHANNEL_COUNT];
static float s_standby_ua[APP_PCBA_CHANNEL_COUNT];
static float s_work_ua[APP_PCBA_CHANNEL_COUNT];
static uint32_t s_standby_variance_ua2[APP_PCBA_CHANNEL_COUNT];
static uint32_t s_work_variance_ua2[APP_PCBA_CHANNEL_COUNT];
static uint32_t s_standby_samples_ua_x100[APP_PCBA_CHANNEL_COUNT][APP_PCBA_CURRENT_SAMPLE_COUNT];
static uint32_t s_work_samples_ua_x100[APP_PCBA_CHANNEL_COUNT][APP_PCBA_CURRENT_SAMPLE_COUNT];
static uint8_t s_standby_valid[APP_PCBA_CHANNEL_COUNT];
static uint8_t s_work_valid[APP_PCBA_CHANNEL_COUNT];

static int adc_single_conversion(uint16_t *raw)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return -1;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return -1;
    }
    if (raw != 0) {
        *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    } else {
        (void)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return 0;
}

static uint16_t read_adc_raw(uint32_t adc_channel)
{
    ADC_ChannelConfTypeDef channel = {0};
    uint32_t raw_sum = 0u;

    channel.Channel = adc_channel;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
        return 0u;
    }
    if (adc_single_conversion(0) != 0) {
        return 0u;
    }

    for (uint8_t sample = 0u; sample < APP_PCBA_CURRENT_ADC_AVERAGE_SAMPLES; ++sample) {
        uint16_t raw = 0u;
        if (adc_single_conversion(&raw) != 0) {
            return 0u;
        }
        raw_sum += raw;
    }

    return (uint16_t)((raw_sum + (APP_PCBA_CURRENT_ADC_AVERAGE_SAMPLES / 2u)) /
                      APP_PCBA_CURRENT_ADC_AVERAGE_SAMPLES);
}

static float raw_to_adc_mv(uint16_t raw)
{
    return ((float)raw * (float)AppAdcCalibration_GetVddaMv()) / 4095.0f;
}

static float raw_to_current_ua(uint16_t raw, uint32_t shunt_mohm)
{
    const float adc_mv = raw_to_adc_mv(raw);
    const float denom = (float)shunt_mohm * (float)APP_PCBA_CURRENT_AMP_GAIN;

    if (denom <= 0.0f) {
        return 0.0f;
    }

    return (adc_mv * 1000000.0f) / denom;
}

static uint32_t current_ua_to_x100(float current_ua)
{
    if (current_ua <= 0.0f) {
        return 0u;
    }

    return (uint32_t)(current_ua * 100.0f + 0.5f);
}

static uint32_t current_ua_x100_to_ua(uint32_t current_ua_x100)
{
    return (current_ua_x100 + 50u) / 100u;
}

static uint32_t work_shunt_mohm(void)
{
    const uint32_t low_branch_mohm = APP_PCBA_50MA_SHUNT_MOHM + APP_PCBA_50MA_NMOS_RDS_ON_MOHM;
    const uint64_t numerator = (uint64_t)APP_PCBA_STANDBY_SHUNT_MOHM * (uint64_t)low_branch_mohm;
    const uint32_t denominator = APP_PCBA_STANDBY_SHUNT_MOHM + low_branch_mohm;

    if (denominator == 0u || low_branch_mohm == 0u) {
        return APP_PCBA_50MA_SHUNT_MOHM;
    }
    return (uint32_t)((numerator + (denominator / 2u)) / denominator);
}

static uint32_t compute_variance_ua2(const uint32_t *samples_ua_x100, uint32_t mean_ua_x100)
{
    uint64_t variance_sum = 0u;
    const int32_t mean_ua = (int32_t)current_ua_x100_to_ua(mean_ua_x100);

    for (uint8_t sample = 0u; sample < APP_PCBA_CURRENT_SAMPLE_COUNT; ++sample) {
        const int32_t sample_ua = (int32_t)current_ua_x100_to_ua(samples_ua_x100[sample]);
        const int32_t diff = sample_ua - mean_ua;
        variance_sum += (uint64_t)((int64_t)diff * (int64_t)diff);
    }

    variance_sum = (variance_sum + (APP_PCBA_CURRENT_SAMPLE_COUNT / 2u)) / APP_PCBA_CURRENT_SAMPLE_COUNT;
    if (variance_sum > 0xFFFFFFFFu) {
        variance_sum = 0xFFFFFFFFu;
    }

    return (uint32_t)variance_sum;
}

void AppCurrent_Init(void)
{
    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        s_raw[i] = 0u;
        s_standby_ua[i] = 0.0f;
        s_work_ua[i] = 0.0f;
        s_standby_variance_ua2[i] = 0u;
        s_work_variance_ua2[i] = 0u;
        s_standby_valid[i] = 0u;
        s_work_valid[i] = 0u;
        for (uint8_t sample = 0u; sample < APP_PCBA_CURRENT_SAMPLE_COUNT; ++sample) {
            s_standby_samples_ua_x100[i][sample] = 0u;
            s_work_samples_ua_x100[i][sample] = 0u;
        }
    }
}

int AppCurrent_CaptureAll(AppCurrentMode mode)
{
    uint32_t current_sum_ua_x100[APP_PCBA_CHANNEL_COUNT] = {0u};
    uint32_t raw_sum[APP_PCBA_CHANNEL_COUNT] = {0u};
    uint32_t (*dest_samples)[APP_PCBA_CURRENT_SAMPLE_COUNT] =
        mode == APP_CURRENT_MODE_STANDBY ? s_standby_samples_ua_x100 : s_work_samples_ua_x100;
    float *dest_current_ua = mode == APP_CURRENT_MODE_STANDBY ? s_standby_ua : s_work_ua;
    uint32_t *dest_variance_ua2 = mode == APP_CURRENT_MODE_STANDBY ? s_standby_variance_ua2 : s_work_variance_ua2;
    uint8_t *dest_valid = mode == APP_CURRENT_MODE_STANDBY ? s_standby_valid : s_work_valid;
    const uint32_t shunt_mohm = mode == APP_CURRENT_MODE_STANDBY ? APP_PCBA_STANDBY_SHUNT_MOHM : work_shunt_mohm();

    (void)AppAdcCalibration_Refresh();

    for (uint8_t sample = 0u; sample < APP_PCBA_CURRENT_SAMPLE_COUNT; ++sample) {
        for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
            const uint16_t raw = read_adc_raw(s_current_adc_channels[i]);
            const uint32_t current_ua_x100 = current_ua_to_x100(raw_to_current_ua(raw, shunt_mohm));

            dest_samples[i][sample] = current_ua_x100;
            current_sum_ua_x100[i] += current_ua_x100;
            raw_sum[i] += raw;
        }
        if ((sample + 1u) < APP_PCBA_CURRENT_SAMPLE_COUNT) {
            HAL_Delay(APP_PCBA_CURRENT_SAMPLE_INTERVAL_MS);
        }
    }

    for (uint8_t i = 0u; i < APP_PCBA_CHANNEL_COUNT; ++i) {
        const uint32_t average_ua_x100 =
            (current_sum_ua_x100[i] + (APP_PCBA_CURRENT_SAMPLE_COUNT / 2u)) / APP_PCBA_CURRENT_SAMPLE_COUNT;
        s_raw[i] = (uint16_t)((raw_sum[i] + (APP_PCBA_CURRENT_SAMPLE_COUNT / 2u)) /
                              APP_PCBA_CURRENT_SAMPLE_COUNT);
        dest_current_ua[i] = average_ua_x100 / 100.0f;
        dest_variance_ua2[i] = compute_variance_ua2(dest_samples[i], average_ua_x100);
        dest_valid[i] = 1u;
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

float AppCurrent_GetStandbyUa(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0.0f;
    }
    return s_standby_ua[channel - 1u];
}

float AppCurrent_GetWorkUa(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0.0f;
    }
    return s_work_ua[channel - 1u];
}

uint32_t AppCurrent_GetStandbyVarianceUa2(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_standby_variance_ua2[channel - 1u];
}

uint32_t AppCurrent_GetWorkVarianceUa2(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_work_variance_ua2[channel - 1u];
}

uint32_t AppCurrent_GetStandbySampleUaX100(uint8_t channel, uint8_t sample_index)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        sample_index >= APP_PCBA_CURRENT_SAMPLE_COUNT) {
        return 0u;
    }
    return s_standby_samples_ua_x100[channel - 1u][sample_index];
}

uint32_t AppCurrent_GetWorkSampleUaX100(uint8_t channel, uint8_t sample_index)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        sample_index >= APP_PCBA_CURRENT_SAMPLE_COUNT) {
        return 0u;
    }
    return s_work_samples_ua_x100[channel - 1u][sample_index];
}

uint8_t AppCurrent_IsStandbyValid(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_standby_valid[channel - 1u];
}

uint8_t AppCurrent_IsWorkValid(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return 0u;
    }
    return s_work_valid[channel - 1u];
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
