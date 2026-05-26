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

typedef struct { __IO uint32_t RESERVED[32]; } ADC_TypeDef;
typedef struct { __IO uint32_t RESERVED[16]; } SPI_TypeDef;
typedef struct { __IO uint32_t RESERVED[16]; } USART_TypeDef;

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

extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif
