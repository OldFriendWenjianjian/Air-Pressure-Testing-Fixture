#include "stm32f1xx_hal.h"

static volatile uint32_t s_tick;

__WEAK void HAL_MspInit(void)
{
}

void HAL_Init(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    HAL_MspInit();
}

uint32_t HAL_GetTick(void)
{
    return s_tick;
}

void HAL_IncTick(void)
{
    ++s_tick;
}

void HAL_Delay(uint32_t delay_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < delay_ms) {
    }
}

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return SystemCoreClock;
}

HAL_StatusTypeDef HAL_RCC_OscConfig(const RCC_OscInitTypeDef *osc)
{
    (void)osc;
    SystemCoreClock = 72000000U;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_RCC_ClockConfig(const RCC_ClkInitTypeDef *clk, uint32_t latency)
{
    (void)clk;
    (void)latency;
    SystemCoreClock = 72000000U;
    SysTick_Config(SystemCoreClock / 1000U);
    return HAL_OK;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    if (port == 0 || init == 0) {
        return;
    }

    for (uint32_t bit = 0U; bit < 16U; ++bit) {
        uint16_t pin = (uint16_t)(1U << bit);
        if ((init->Pin & pin) == 0U) {
            continue;
        }

        __IO uint32_t *cr = bit < 8U ? &port->CRL : &port->CRH;
        uint32_t shift = (bit & 7U) * 4U;
        uint32_t cfg = 0x4U;

        if (init->Mode == GPIO_MODE_OUTPUT_PP) {
            cfg = 0x2U;
        } else if (init->Mode == GPIO_MODE_AF_PP) {
            cfg = 0xAU;
        } else if (init->Mode == GPIO_MODE_ANALOG) {
            cfg = 0x0U;
        } else if (init->Mode == GPIO_MODE_INPUT && init->Pull == GPIO_PULLUP) {
            cfg = 0x8U;
            port->BSRR = pin;
        }

        *cr = (*cr & ~(0xFU << shift)) | (cfg << shift);
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        port->BSRR = pin;
    } else {
        port->BRR = pin;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->IDR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *channel)
{
    (void)hadc;
    (void)channel;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout)
{
    (void)hadc;
    (void)timeout;
    return HAL_OK;
}

uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return 0U;
}

HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)hspi;
    (void)data;
    (void)len;
    (void)timeout;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)hspi;
    (void)timeout;
    for (uint16_t i = 0U; i < len; ++i) {
        data[i] = 0xFFU;
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout)
{
    (void)hspi;
    (void)timeout;
    for (uint16_t i = 0U; i < len; ++i) {
        rx[i] = tx != 0 ? tx[i] : 0xFFU;
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
{
    (void)huart;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)huart;
    (void)data;
    (void)len;
    (void)timeout;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)huart;
    (void)data;
    (void)len;
    (void)timeout;
    return HAL_TIMEOUT;
}
