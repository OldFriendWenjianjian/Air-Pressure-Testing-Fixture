#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef gpio = {0};

    if (hadc->Instance != ADC1) {
        return;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio = {0};

    if (hspi->Instance == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;
        HAL_GPIO_Init(GPIOB, &gpio);

        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pin = GPIO_PIN_14;
        HAL_GPIO_Init(GPIOB, &gpio);
    } else if (hspi->Instance == SPI3) {
        __HAL_RCC_SPI3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio.Pin = GPIO_PIN_3 | GPIO_PIN_5;
        HAL_GPIO_Init(GPIOB, &gpio);

        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pin = GPIO_PIN_4;
        HAL_GPIO_Init(GPIOB, &gpio);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    if (huart->Instance == USART1) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_9;
        gpio.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
    } else if (huart->Instance == USART2) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_5;
        gpio.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOD, &gpio);
        gpio.Pin = GPIO_PIN_6;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOD, &gpio);
    } else if (huart->Instance == USART3) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_8;
        gpio.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOD, &gpio);
        gpio.Pin = GPIO_PIN_9;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOD, &gpio);
    } else if (huart->Instance == UART4) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOC, &gpio);
        gpio.Pin = GPIO_PIN_11;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &gpio);
    } else if (huart->Instance == UART5) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_12;
        gpio.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOC, &gpio);
        gpio.Pin = GPIO_PIN_2;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOD, &gpio);
    }
}
