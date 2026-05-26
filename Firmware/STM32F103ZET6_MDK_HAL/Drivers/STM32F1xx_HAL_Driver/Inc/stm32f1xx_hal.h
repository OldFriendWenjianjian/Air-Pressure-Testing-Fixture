#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx.h"
#include <stddef.h>

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#define HAL_OK                    0x00U
#define HAL_ERROR                 0x01U
#define HAL_BUSY                  0x02U
#define HAL_TIMEOUT               0x03U
typedef uint32_t HAL_StatusTypeDef;

#define ENABLE                    1U
#define DISABLE                   0U

#define GPIO_PIN_0                ((uint16_t)0x0001)
#define GPIO_PIN_1                ((uint16_t)0x0002)
#define GPIO_PIN_2                ((uint16_t)0x0004)
#define GPIO_PIN_3                ((uint16_t)0x0008)
#define GPIO_PIN_4                ((uint16_t)0x0010)
#define GPIO_PIN_5                ((uint16_t)0x0020)
#define GPIO_PIN_6                ((uint16_t)0x0040)
#define GPIO_PIN_7                ((uint16_t)0x0080)
#define GPIO_PIN_8                ((uint16_t)0x0100)
#define GPIO_PIN_9                ((uint16_t)0x0200)
#define GPIO_PIN_10               ((uint16_t)0x0400)
#define GPIO_PIN_11               ((uint16_t)0x0800)
#define GPIO_PIN_12               ((uint16_t)0x1000)
#define GPIO_PIN_13               ((uint16_t)0x2000)
#define GPIO_PIN_14               ((uint16_t)0x4000)
#define GPIO_PIN_15               ((uint16_t)0x8000)

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

#define GPIO_MODE_INPUT           0x00000000U
#define GPIO_MODE_OUTPUT_PP       0x00000001U
#define GPIO_MODE_AF_PP           0x00000002U
#define GPIO_MODE_ANALOG          0x00000003U
#define GPIO_NOPULL               0x00000000U
#define GPIO_PULLUP               0x00000001U
#define GPIO_SPEED_FREQ_LOW       0x00000002U
#define GPIO_SPEED_FREQ_HIGH      0x00000003U

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct {
    ADC_TypeDef *Instance;
    struct {
        uint32_t ScanConvMode;
        uint32_t ContinuousConvMode;
        uint32_t DiscontinuousConvMode;
        uint32_t ExternalTrigConv;
        uint32_t DataAlign;
        uint32_t NbrOfConversion;
    } Init;
} ADC_HandleTypeDef;

typedef struct {
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;

#define ADC_SCAN_DISABLE          0U
#define ADC_SOFTWARE_START        0U
#define ADC_DATAALIGN_RIGHT       0U
#define ADC_REGULAR_RANK_1        1U
#define ADC_SAMPLETIME_55CYCLES_5 0U
#define ADC_CHANNEL_0             0U
#define ADC_CHANNEL_1             1U
#define ADC_CHANNEL_2             2U
#define ADC_CHANNEL_3             3U
#define ADC_CHANNEL_4             4U
#define ADC_CHANNEL_5             5U
#define ADC_CHANNEL_6             6U
#define ADC_CHANNEL_7             7U
#define ADC_CHANNEL_8             8U
#define ADC_CHANNEL_9             9U
#define ADC_CHANNEL_10            10U
#define ADC_CHANNEL_11            11U
#define ADC_CHANNEL_12            12U
#define ADC_CHANNEL_13            13U

typedef struct {
    SPI_TypeDef *Instance;
    struct {
        uint32_t Mode;
        uint32_t Direction;
        uint32_t DataSize;
        uint32_t CLKPolarity;
        uint32_t CLKPhase;
        uint32_t NSS;
        uint32_t BaudRatePrescaler;
        uint32_t FirstBit;
        uint32_t TIMode;
        uint32_t CRCCalculation;
        uint32_t CRCPolynomial;
    } Init;
} SPI_HandleTypeDef;

#define SPI_MODE_MASTER           0U
#define SPI_DIRECTION_2LINES      0U
#define SPI_DATASIZE_8BIT         0U
#define SPI_POLARITY_LOW          0U
#define SPI_PHASE_1EDGE           0U
#define SPI_NSS_SOFT              0U
#define SPI_BAUDRATEPRESCALER_4   4U
#define SPI_BAUDRATEPRESCALER_8   8U
#define SPI_FIRSTBIT_MSB          0U
#define SPI_TIMODE_DISABLE        0U
#define SPI_CRCCALCULATION_DISABLE 0U

typedef struct {
    USART_TypeDef *Instance;
    struct {
        uint32_t BaudRate;
        uint32_t WordLength;
        uint32_t StopBits;
        uint32_t Parity;
        uint32_t Mode;
        uint32_t HwFlowCtl;
        uint32_t OverSampling;
    } Init;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B        0U
#define UART_STOPBITS_1           0U
#define UART_PARITY_NONE          0U
#define UART_MODE_TX_RX           0U
#define UART_HWCONTROL_NONE       0U
#define UART_OVERSAMPLING_16      0U

typedef struct {
    uint32_t OscillatorType;
    uint32_t HSEState;
    uint32_t HSEPredivValue;
    uint32_t HSIState;
    struct {
        uint32_t PLLState;
        uint32_t PLLSource;
        uint32_t PLLMUL;
    } PLL;
} RCC_OscInitTypeDef;

typedef struct {
    uint32_t ClockType;
    uint32_t SYSCLKSource;
    uint32_t AHBCLKDivider;
    uint32_t APB1CLKDivider;
    uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

#define RCC_OSCILLATORTYPE_HSE    1U
#define RCC_HSE_ON                1U
#define RCC_HSE_PREDIV_DIV1       1U
#define RCC_HSI_ON                1U
#define RCC_PLL_ON                1U
#define RCC_PLLSOURCE_HSE         1U
#define RCC_PLL_MUL9              9U
#define RCC_CLOCKTYPE_HCLK        0x01U
#define RCC_CLOCKTYPE_SYSCLK      0x02U
#define RCC_CLOCKTYPE_PCLK1       0x04U
#define RCC_CLOCKTYPE_PCLK2       0x08U
#define RCC_SYSCLKSOURCE_PLLCLK   1U
#define RCC_SYSCLK_DIV1           1U
#define RCC_HCLK_DIV1             1U
#define RCC_HCLK_DIV2             2U
#define FLASH_LATENCY_2           2U

#define __HAL_RCC_AFIO_CLK_ENABLE()       (RCC->APB2ENR |= (1UL << 0))
#define __HAL_RCC_PWR_CLK_ENABLE()        (RCC->APB1ENR |= (1UL << 28))
#define __HAL_RCC_GPIOA_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 2))
#define __HAL_RCC_GPIOB_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 3))
#define __HAL_RCC_GPIOC_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 4))
#define __HAL_RCC_GPIOD_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 5))
#define __HAL_RCC_GPIOE_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 6))
#define __HAL_RCC_GPIOF_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 7))
#define __HAL_RCC_GPIOG_CLK_ENABLE()      (RCC->APB2ENR |= (1UL << 8))
#define __HAL_RCC_ADC1_CLK_ENABLE()       (RCC->APB2ENR |= (1UL << 9))
#define __HAL_RCC_SPI2_CLK_ENABLE()       (RCC->APB1ENR |= (1UL << 14))
#define __HAL_RCC_SPI3_CLK_ENABLE()       (RCC->APB1ENR |= (1UL << 15))
#define __HAL_RCC_USART1_CLK_ENABLE()     (RCC->APB2ENR |= (1UL << 14))
#define __HAL_RCC_USART2_CLK_ENABLE()     (RCC->APB1ENR |= (1UL << 17))
#define __HAL_RCC_USART3_CLK_ENABLE()     (RCC->APB1ENR |= (1UL << 18))
#define __HAL_RCC_UART4_CLK_ENABLE()      (RCC->APB1ENR |= (1UL << 19))
#define __HAL_RCC_UART5_CLK_ENABLE()      (RCC->APB1ENR |= (1UL << 20))
#define __HAL_AFIO_REMAP_SWJ_NOJTAG()     do { AFIO->MAPR = (AFIO->MAPR & ~(7UL << 24)) | (2UL << 24); } while (0)
#define __HAL_AFIO_REMAP_USART2_ENABLE()  (AFIO->MAPR |= (1UL << 3))
#define __HAL_AFIO_REMAP_USART3_ENABLE()  do { AFIO->MAPR = (AFIO->MAPR & ~(3UL << 4)) | (3UL << 4); } while (0)

void HAL_Init(void);
uint32_t HAL_GetTick(void);
void HAL_IncTick(void);
void HAL_Delay(uint32_t delay_ms);
uint32_t HAL_RCC_GetHCLKFreq(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(const RCC_OscInitTypeDef *osc);
HAL_StatusTypeDef HAL_RCC_ClockConfig(const RCC_ClkInitTypeDef *clk, uint32_t latency);

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *channel);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc);

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout);

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *data, uint16_t len, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
