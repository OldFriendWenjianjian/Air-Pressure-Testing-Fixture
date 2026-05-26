#include "app_keys.h"
#include "board_pins.h"

int AppKeys_Key1Pressed(void)
{
    return HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET;
}

int AppKeys_Key2Pressed(void)
{
    return HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET;
}

int AppKeys_PressSwitchActive(void)
{
    return HAL_GPIO_ReadPin(PRESS_SWITCH_GPIO_Port, PRESS_SWITCH_Pin) == GPIO_PIN_RESET;
}

int AppKeys_Key1HeldAtBoot(uint32_t hold_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < hold_ms) {
        if (!AppKeys_Key1Pressed()) {
            return 0;
        }
    }

    return 1;
}
