#include "app_power.h"
#include "board_pins.h"

static uint8_t s_power_5v_enabled;
static uint8_t s_power_45v_enabled;
static uint8_t s_power_50ma_enabled;

void AppPower_AllOff(void)
{
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_50MA_ENABLE_GPIO_Port, PCBA_50MA_ENABLE_Pin, GPIO_PIN_RESET);
    s_power_5v_enabled = 0u;
    s_power_45v_enabled = 0u;
    s_power_50ma_enabled = 0u;
}

void AppPower_Enable5V(void)
{
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_SET);
    s_power_5v_enabled = 1u;
    s_power_45v_enabled = 0u;
}

void AppPower_Enable45V(void)
{
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_SET);
    s_power_5v_enabled = 0u;
    s_power_45v_enabled = 1u;
}

void AppPower_Enable50mATestCircuit(int enable)
{
    HAL_GPIO_WritePin(PCBA_50MA_ENABLE_GPIO_Port,
                      PCBA_50MA_ENABLE_Pin,
                      enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_power_50ma_enabled = enable ? 1u : 0u;
}

uint8_t AppPower_Is5VEnabled(void)
{
    return s_power_5v_enabled;
}

uint8_t AppPower_Is45VEnabled(void)
{
    return s_power_45v_enabled;
}

uint8_t AppPower_Is50mATestCircuitEnabled(void)
{
    return s_power_50ma_enabled;
}
