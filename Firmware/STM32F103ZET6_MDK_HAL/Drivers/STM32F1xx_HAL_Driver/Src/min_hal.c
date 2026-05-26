#include "stm32f1xx_hal.h"

static volatile uint32_t s_tick;
static uint32_t s_adc_channel;

#define RCC_CR_HSEON              (1UL << 16)
#define RCC_CR_HSERDY             (1UL << 17)
#define RCC_CR_PLLON              (1UL << 24)
#define RCC_CR_PLLRDY             (1UL << 25)
#define RCC_CFGR_SW_PLL           (2UL << 0)
#define RCC_CFGR_SWS_PLL          (2UL << 2)
#define RCC_CFGR_HPRE_DIV1        (0UL << 4)
#define RCC_CFGR_PPRE1_DIV2       (4UL << 8)
#define RCC_CFGR_PPRE2_DIV1       (0UL << 11)
#define RCC_CFGR_ADCPRE_DIV6      (2UL << 14)
#define RCC_CFGR_PLLSRC_HSE       (1UL << 16)
#define RCC_CFGR_PLLMULL9         (7UL << 18)

#define FLASH_ACR_PRFTBE          (1UL << 4)

#define ADC_SR_EOC                (1UL << 1)
#define ADC_CR2_ADON              (1UL << 0)
#define ADC_CR2_CAL               (1UL << 2)
#define ADC_CR2_RSTCAL            (1UL << 3)
#define ADC_CR2_EXTSEL_SWSTART    (7UL << 17)
#define ADC_CR2_SWSTART           (1UL << 22)
#define ADC_CR2_EXTTRIG           (1UL << 20)

#define SPI_CR1_CPHA              (1UL << 0)
#define SPI_CR1_CPOL              (1UL << 1)
#define SPI_CR1_MSTR              (1UL << 2)
#define SPI_CR1_BR_Pos            3U
#define SPI_CR1_SPE               (1UL << 6)
#define SPI_CR1_LSBFIRST          (1UL << 7)
#define SPI_CR1_SSI               (1UL << 8)
#define SPI_CR1_SSM               (1UL << 9)
#define SPI_SR_RXNE               (1UL << 0)
#define SPI_SR_TXE                (1UL << 1)
#define SPI_SR_BSY                (1UL << 7)

#define USART_SR_RXNE             (1UL << 5)
#define USART_SR_TC               (1UL << 6)
#define USART_SR_TXE              (1UL << 7)
#define USART_CR1_RE              (1UL << 2)
#define USART_CR1_TE              (1UL << 3)
#define USART_CR1_UE              (1UL << 13)

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

    RCC->CR |= RCC_CR_HSEON;
    uint32_t start = HAL_GetTick();
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
        if ((HAL_GetTick() - start) > 100U) {
            return HAL_TIMEOUT;
        }
    }

    RCC->CFGR &= ~((0xFUL << 18) | (1UL << 16));
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    start = HAL_GetTick();
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
        if ((HAL_GetTick() - start) > 100U) {
            return HAL_TIMEOUT;
        }
    }

    SystemCoreClock = 72000000U;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_RCC_ClockConfig(const RCC_ClkInitTypeDef *clk, uint32_t latency)
{
    (void)clk;

    FLASH->ACR = (FLASH->ACR & ~0x7UL) | (latency & 0x7UL) | FLASH_ACR_PRFTBE;
    RCC->CFGR &= ~((0xFUL << 4) | (0x7UL << 8) | (0x7UL << 11) | (0x3UL << 14));
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_ADCPRE_DIV6;
    RCC->CFGR = (RCC->CFGR & ~0x3UL) | RCC_CFGR_SW_PLL;

    uint32_t start = HAL_GetTick();
    while ((RCC->CFGR & (0x3UL << 2)) != RCC_CFGR_SWS_PLL) {
        if ((HAL_GetTick() - start) > 100U) {
            return HAL_TIMEOUT;
        }
    }

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
    if (hadc == 0 || hadc->Instance == 0) {
        return HAL_ERROR;
    }

    hadc->Instance->CR1 = 0U;
    hadc->Instance->CR2 = ADC_CR2_ADON;
    HAL_Delay(1U);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc)
{
    if (hadc == 0 || hadc->Instance == 0) {
        return HAL_ERROR;
    }

    hadc->Instance->CR2 |= ADC_CR2_RSTCAL;
    uint32_t start = HAL_GetTick();
    while ((hadc->Instance->CR2 & ADC_CR2_RSTCAL) != 0U) {
        if ((HAL_GetTick() - start) > 10U) {
            return HAL_TIMEOUT;
        }
    }

    hadc->Instance->CR2 |= ADC_CR2_CAL;
    start = HAL_GetTick();
    while ((hadc->Instance->CR2 & ADC_CR2_CAL) != 0U) {
        if ((HAL_GetTick() - start) > 10U) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *channel)
{
    if (hadc == 0 || hadc->Instance == 0 || channel == 0 || channel->Channel > 17U) {
        return HAL_ERROR;
    }

    s_adc_channel = channel->Channel;
    hadc->Instance->SQR1 = 0U;
    hadc->Instance->SQR2 = 0U;
    hadc->Instance->SQR3 = s_adc_channel;

    if (s_adc_channel <= 9U) {
        uint32_t shift = s_adc_channel * 3U;
        hadc->Instance->SMPR2 = (hadc->Instance->SMPR2 & ~(7UL << shift)) | (5UL << shift);
    } else {
        uint32_t shift = (s_adc_channel - 10U) * 3U;
        hadc->Instance->SMPR1 = (hadc->Instance->SMPR1 & ~(7UL << shift)) | (5UL << shift);
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc)
{
    if (hadc == 0 || hadc->Instance == 0) {
        return HAL_ERROR;
    }

    hadc->Instance->SR = 0U;
    hadc->Instance->CR2 = (hadc->Instance->CR2 & ~(7UL << 17)) |
                          ADC_CR2_ADON | ADC_CR2_EXTSEL_SWSTART |
                          ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout)
{
    if (hadc == 0 || hadc->Instance == 0) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    while ((hadc->Instance->SR & ADC_SR_EOC) == 0U) {
        if ((HAL_GetTick() - start) > timeout) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc)
{
    if (hadc == 0 || hadc->Instance == 0) {
        return 0U;
    }

    return hadc->Instance->DR & 0xFFFFU;
}

HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{
    if (hspi == 0 || hspi->Instance == 0) {
        return HAL_ERROR;
    }

    uint32_t br = 0U;
    if (hspi->Init.BaudRatePrescaler >= 8U) {
        br = 2U;
    } else if (hspi->Init.BaudRatePrescaler >= 4U) {
        br = 1U;
    }

    hspi->Instance->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                          (br << SPI_CR1_BR_Pos);
    if (hspi->Init.CLKPolarity != 0U) {
        hspi->Instance->CR1 |= SPI_CR1_CPOL;
    }
    if (hspi->Init.CLKPhase != 0U) {
        hspi->Instance->CR1 |= SPI_CR1_CPHA;
    }
    if (hspi->Init.FirstBit != 0U) {
        hspi->Instance->CR1 |= SPI_CR1_LSBFIRST;
    }
    hspi->Instance->CR1 |= SPI_CR1_SPE;

    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (hspi == 0 || hspi->Instance == 0 || (data == 0 && len > 0U)) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0U; i < len; ++i) {
        while ((hspi->Instance->SR & SPI_SR_TXE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        *(__IO uint8_t *)&hspi->Instance->DR = data[i];
        while ((hspi->Instance->SR & SPI_SR_RXNE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        (void)*(__IO uint8_t *)&hspi->Instance->DR;
    }

    while ((hspi->Instance->SR & SPI_SR_BSY) != 0U) {
        if ((HAL_GetTick() - start) > timeout) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout)
{
    uint8_t dummy = 0xFFU;

    if (hspi == 0 || hspi->Instance == 0 || (data == 0 && len > 0U)) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0U; i < len; ++i) {
        while ((hspi->Instance->SR & SPI_SR_TXE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        *(__IO uint8_t *)&hspi->Instance->DR = dummy;
        while ((hspi->Instance->SR & SPI_SR_RXNE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        data[i] = *(__IO uint8_t *)&hspi->Instance->DR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout)
{
    if (hspi == 0 || hspi->Instance == 0 || (rx == 0 && len > 0U)) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0U; i < len; ++i) {
        uint8_t out = tx != 0 ? tx[i] : 0xFFU;
        while ((hspi->Instance->SR & SPI_SR_TXE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        *(__IO uint8_t *)&hspi->Instance->DR = out;
        while ((hspi->Instance->SR & SPI_SR_RXNE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        rx[i] = *(__IO uint8_t *)&hspi->Instance->DR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
{
    if (huart == 0 || huart->Instance == 0 || huart->Init.BaudRate == 0U) {
        return HAL_ERROR;
    }

    uint32_t pclk = (huart->Instance == USART1) ? 72000000U : 36000000U;
    huart->Instance->CR1 = 0U;
    huart->Instance->CR2 = 0U;
    huart->Instance->CR3 = 0U;
    huart->Instance->BRR = (pclk + (huart->Init.BaudRate / 2U)) / huart->Init.BaudRate;
    huart->Instance->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (huart == 0 || huart->Instance == 0 || (data == 0 && len > 0U)) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0U; i < len; ++i) {
        while ((huart->Instance->SR & USART_SR_TXE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        huart->Instance->DR = data[i];
    }
    while ((huart->Instance->SR & USART_SR_TC) == 0U) {
        if ((HAL_GetTick() - start) > timeout) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (huart == 0 || huart->Instance == 0 || (data == 0 && len > 0U)) {
        return HAL_ERROR;
    }

    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0U; i < len; ++i) {
        while ((huart->Instance->SR & USART_SR_RXNE) == 0U) {
            if ((HAL_GetTick() - start) > timeout) {
                return HAL_TIMEOUT;
            }
        }
        data[i] = (uint8_t)(huart->Instance->DR & 0xFFU);
    }

    return HAL_OK;
}
