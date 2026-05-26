#include "stm32f1xx.h"

uint32_t SystemCoreClock = 72000000U;

void SystemInit(void)
{
    SystemCoreClock = 72000000U;
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = 72000000U;
}
