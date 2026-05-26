#ifndef STM32F1XX_H
#define STM32F1XX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    NonMaskableInt_IRQn = -14,
    HardFault_IRQn      = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn       = -11,
    UsageFault_IRQn     = -10,
    SVCall_IRQn         = -5,
    DebugMonitor_IRQn   = -4,
    PendSV_IRQn         = -2,
    SysTick_IRQn        = -1
} IRQn_Type;

#define __CM3_REV                 0x0200U
#define __MPU_PRESENT             0U
#define __NVIC_PRIO_BITS          4U
#define __Vendor_SysTickConfig    0U

#include "core_cm3.h"

typedef struct {
    __IO uint32_t CRL;
    __IO uint32_t CRH;
    __IO uint32_t IDR;
    __IO uint32_t ODR;
    __IO uint32_t BSRR;
    __IO uint32_t BRR;
    __IO uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
    __IO uint32_t EVCR;
    __IO uint32_t MAPR;
    __IO uint32_t EXTICR[4];
    uint32_t RESERVED0;
    __IO uint32_t MAPR2;
} AFIO_TypeDef;

typedef struct {
    __IO uint32_t CR;
    __IO uint32_t CFGR;
    __IO uint32_t CIR;
    __IO uint32_t APB2RSTR;
    __IO uint32_t APB1RSTR;
    __IO uint32_t AHBENR;
    __IO uint32_t APB2ENR;
    __IO uint32_t APB1ENR;
    __IO uint32_t BDCR;
    __IO uint32_t CSR;
} RCC_TypeDef;

typedef struct {
    __IO uint32_t ACR;
    __IO uint32_t KEYR;
    __IO uint32_t OPTKEYR;
    __IO uint32_t SR;
    __IO uint32_t CR;
    __IO uint32_t AR;
    __IO uint32_t RESERVED;
    __IO uint32_t OBR;
    __IO uint32_t WRPR;
} FLASH_TypeDef;

typedef struct {
    __IO uint32_t SR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SMPR1;
    __IO uint32_t SMPR2;
    __IO uint32_t JOFR1;
    __IO uint32_t JOFR2;
    __IO uint32_t JOFR3;
    __IO uint32_t JOFR4;
    __IO uint32_t HTR;
    __IO uint32_t LTR;
    __IO uint32_t SQR1;
    __IO uint32_t SQR2;
    __IO uint32_t SQR3;
    __IO uint32_t JSQR;
    __IO uint32_t JDR1;
    __IO uint32_t JDR2;
    __IO uint32_t JDR3;
    __IO uint32_t JDR4;
    __IO uint32_t DR;
} ADC_TypeDef;

typedef struct {
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SR;
    __IO uint32_t DR;
    __IO uint32_t CRCPR;
    __IO uint32_t RXCRCR;
    __IO uint32_t TXCRCR;
    __IO uint32_t I2SCFGR;
    __IO uint32_t I2SPR;
} SPI_TypeDef;

typedef struct {
    __IO uint32_t SR;
    __IO uint32_t DR;
    __IO uint32_t BRR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t CR3;
    __IO uint32_t GTPR;
} USART_TypeDef;

#define AFIO_BASE                 0x40010000UL
#define GPIOA_BASE                0x40010800UL
#define GPIOB_BASE                0x40010C00UL
#define GPIOC_BASE                0x40011000UL
#define GPIOD_BASE                0x40011400UL
#define GPIOE_BASE                0x40011800UL
#define GPIOF_BASE                0x40011C00UL
#define GPIOG_BASE                0x40012000UL
#define ADC1_BASE                 0x40012400UL
#define SPI2_BASE                 0x40003800UL
#define SPI3_BASE                 0x40003C00UL
#define USART1_BASE               0x40013800UL
#define USART2_BASE               0x40004400UL
#define USART3_BASE               0x40004800UL
#define UART4_BASE                0x40004C00UL
#define UART5_BASE                0x40005000UL
#define RCC_BASE                  0x40021000UL
#define FLASH_R_BASE              0x40022000UL

#define AFIO                      ((AFIO_TypeDef *)AFIO_BASE)
#define GPIOA                     ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB                     ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC                     ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD                     ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOE                     ((GPIO_TypeDef *)GPIOE_BASE)
#define GPIOF                     ((GPIO_TypeDef *)GPIOF_BASE)
#define GPIOG                     ((GPIO_TypeDef *)GPIOG_BASE)
#define ADC1                      ((ADC_TypeDef *)ADC1_BASE)
#define SPI2                      ((SPI_TypeDef *)SPI2_BASE)
#define SPI3                      ((SPI_TypeDef *)SPI3_BASE)
#define USART1                    ((USART_TypeDef *)USART1_BASE)
#define USART2                    ((USART_TypeDef *)USART2_BASE)
#define USART3                    ((USART_TypeDef *)USART3_BASE)
#define UART4                     ((USART_TypeDef *)UART4_BASE)
#define UART5                     ((USART_TypeDef *)UART5_BASE)
#define RCC                       ((RCC_TypeDef *)RCC_BASE)
#define FLASH                     ((FLASH_TypeDef *)FLASH_R_BASE)

extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif
