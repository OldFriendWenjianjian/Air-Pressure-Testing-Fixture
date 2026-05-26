#include "board_pins.h"

const BoardGpio BOARD_VALVE_PINS[26] = {
    {GPIOE, GPIO_PIN_8},  {GPIOE, GPIO_PIN_9},  {GPIOE, GPIO_PIN_10}, {GPIOE, GPIO_PIN_11},
    {GPIOE, GPIO_PIN_12}, {GPIOE, GPIO_PIN_13}, {GPIOE, GPIO_PIN_14}, {GPIOE, GPIO_PIN_15},
    {GPIOD, GPIO_PIN_0},  {GPIOD, GPIO_PIN_1},  {GPIOD, GPIO_PIN_3},  {GPIOD, GPIO_PIN_4},
    {GPIOD, GPIO_PIN_7},  {GPIOD, GPIO_PIN_10}, {GPIOD, GPIO_PIN_11}, {GPIOD, GPIO_PIN_12},
    {GPIOD, GPIO_PIN_13}, {GPIOD, GPIO_PIN_14}, {GPIOD, GPIO_PIN_15}, {GPIOG, GPIO_PIN_15},
    {GPIOA, GPIO_PIN_15}, {GPIOB, GPIO_PIN_6},  {GPIOB, GPIO_PIN_7},  {GPIOB, GPIO_PIN_8},
    {GPIOB, GPIO_PIN_9},  {GPIOC, GPIO_PIN_6}
};

const BoardGpio BOARD_I2C_SCL_PINS[14] = {
    {GPIOF, GPIO_PIN_0}, {GPIOF, GPIO_PIN_2}, {GPIOF, GPIO_PIN_4}, {GPIOF, GPIO_PIN_6},
    {GPIOF, GPIO_PIN_8}, {GPIOF, GPIO_PIN_10}, {GPIOF, GPIO_PIN_12}, {GPIOF, GPIO_PIN_14},
    {GPIOG, GPIO_PIN_5}, {GPIOG, GPIO_PIN_7}, {GPIOG, GPIO_PIN_9}, {GPIOG, GPIO_PIN_11},
    {GPIOG, GPIO_PIN_13}, {GPIOE, GPIO_PIN_6}
};

const BoardGpio BOARD_I2C_SDA_PINS[14] = {
    {GPIOF, GPIO_PIN_1}, {GPIOF, GPIO_PIN_3}, {GPIOF, GPIO_PIN_5}, {GPIOF, GPIO_PIN_7},
    {GPIOF, GPIO_PIN_9}, {GPIOF, GPIO_PIN_11}, {GPIOF, GPIO_PIN_13}, {GPIOF, GPIO_PIN_15},
    {GPIOG, GPIO_PIN_6}, {GPIOG, GPIO_PIN_8}, {GPIOG, GPIO_PIN_10}, {GPIOG, GPIO_PIN_12},
    {GPIOG, GPIO_PIN_14}, {GPIOE, GPIO_PIN_7}
};

void BoardPins_EnableAllGpioClocks(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

void BoardPins_ConfigOutputsSafe(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint8_t i = 0; i < 26u; ++i) {
        HAL_GPIO_WritePin(BOARD_VALVE_PINS[i].port, BOARD_VALVE_PINS[i].pin, GPIO_PIN_RESET);
        gpio.Pin = BOARD_VALVE_PINS[i].pin;
        HAL_GPIO_Init(BOARD_VALVE_PINS[i].port, &gpio);
    }

    HAL_GPIO_WritePin(PCBA_5V_ENABLE_GPIO_Port, PCBA_5V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_45V_ENABLE_GPIO_Port, PCBA_45V_ENABLE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PCBA_50MA_ENABLE_GPIO_Port, PCBA_50MA_ENABLE_Pin, GPIO_PIN_RESET);
    gpio.Pin = PCBA_5V_ENABLE_Pin;
    HAL_GPIO_Init(PCBA_5V_ENABLE_GPIO_Port, &gpio);
    gpio.Pin = PCBA_45V_ENABLE_Pin;
    HAL_GPIO_Init(PCBA_45V_ENABLE_GPIO_Port, &gpio);
    gpio.Pin = PCBA_50MA_ENABLE_Pin;
    HAL_GPIO_Init(PCBA_50MA_ENABLE_GPIO_Port, &gpio);

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET);

    gpio.Pin = LCD_CS_Pin;
    HAL_GPIO_Init(LCD_CS_GPIO_Port, &gpio);
    gpio.Pin = LCD_RST_Pin | CTP_RST_Pin | W25Q_CS_Pin;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = LCD_INT_Pin | CTP_INT_Pin;
    HAL_GPIO_Init(GPIOG, &gpio);
}

void BoardPins_ConfigKeys(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = KEY1_Pin | KEY2_Pin | KEY3_Pin | KEY4_Pin | PRESS_SWITCH_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);
}

void BoardPins_ConfigSpi3Float(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET);
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void BoardPins_ConfigSpi3Msc(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = W25Q_CS_Pin;
    HAL_GPIO_Init(W25Q_CS_GPIO_Port, &gpio);
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET);
}
