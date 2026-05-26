#include "app_power.h"
#include "board_pins.h"

void AppPower_AllOff(void)
{
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_50MA_ENABLE_GPIO_Port, PCBA_50MA_ENABLE_Pin, GPIO_PIN_RESET);
}

void AppPower_Enable5V(void)
{
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_SET);
}

void AppPower_Enable45V(void)
{
    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_SET);
}

void AppPower_Enable50mATestCircuit(int enable)
{
    HAL_GPIO_WritePin(PCBA_50MA_ENABLE_GPIO_Port,
                      PCBA_50MA_ENABLE_Pin,
                      enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
