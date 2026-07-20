#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
typedef struct {
    uint32_t signature;
    uint32_t fault_type;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t msp;
    uint32_t psp;
    uint32_t exc_return;
} AppFaultRecord;

extern volatile AppFaultRecord g_app_fault_record;

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif
