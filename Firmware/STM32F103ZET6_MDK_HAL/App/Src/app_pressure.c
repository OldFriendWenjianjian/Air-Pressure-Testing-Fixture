#include "app_pressure.h"
#include "app_config.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

static uint16_t s_raw[APP_PRESSURE_SENSOR_COUNT];

static const uint32_t s_adc_channels[APP_PRESSURE_SENSOR_COUNT] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
    ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7,
    ADC_CHANNEL_8, ADC_CHANNEL_9, ADC_CHANNEL_10, ADC_CHANNEL_11,
    ADC_CHANNEL_12, ADC_CHANNEL_13
};

void AppPressure_Init(void)
{
    for (uint8_t i = 0; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        s_raw[i] = 0u;
    }
}

void AppPressure_Task(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    for (uint8_t i = 0; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        /*
         * Hardware note:
         * current pin assignment conflicts PB1 with 50mA enable and PC3 with
         * KEY1. Readings mapped to those ADC channels are placeholders until
         * the final schematic resolves the conflict.
         */
        channel.Channel = s_adc_channels[i];
        if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
            continue;
        }
        if (HAL_ADC_Start(&hadc1) != HAL_OK) {
            continue;
        }
        if (HAL_ADC_PollForConversion(&hadc1, 10u) == HAL_OK) {
            s_raw[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
        }
        HAL_ADC_Stop(&hadc1);
    }
}

uint16_t AppPressure_GetRaw(PressureSensorIndex index)
{
    if ((uint8_t)index >= APP_PRESSURE_SENSOR_COUNT) {
        return 0u;
    }
    return s_raw[(uint8_t)index];
}

uint32_t AppPressure_Get001mmHg(PressureSensorIndex index)
{
    uint16_t raw = AppPressure_GetRaw(index);

    /* Placeholder linear mapping. Calibrate offset/span in hardware bring-up. */
    return ((uint32_t)raw * 300u * APP_PRESSURE_SCALE_PER_MMHG) / 4095u;
}

int AppPressure_IsStable(PressureSensorIndex index, uint32_t target_001mmhg)
{
    uint32_t measured = AppPressure_Get001mmHg(index);
    uint32_t delta = measured > target_001mmhg ? measured - target_001mmhg : target_001mmhg - measured;

    return delta <= ((uint32_t)APP_PRESSURE_STABLE_WINDOW_RAW * APP_PRESSURE_SCALE_PER_MMHG);
}

int AppPressure_AllChannelOutputsNear(uint32_t target_001mmhg)
{
    for (uint8_t i = PRESSURE_SENSOR_CH1; i <= PRESSURE_SENSOR_CH8; ++i) {
        if (!AppPressure_IsStable((PressureSensorIndex)i, target_001mmhg)) {
            return 0;
        }
    }

    return 1;
}
