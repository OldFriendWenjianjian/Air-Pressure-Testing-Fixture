#include "app_config.h"
#include "main.h"
#include "usb_cdc_device.h"
#include "usb_msc_device.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {
    }
}

void MemManage_Handler(void)
{
    while (1) {
    }
}

void BusFault_Handler(void)
{
    while (1) {
    }
}

void UsageFault_Handler(void)
{
    while (1) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void EXTI9_5_IRQHandler(void)
{
#if APP_PC_LINK_JLINK_UART8_ENABLED
    AppJlinkUartControl_ExtiIrqHandler();
#endif
}

void USB_HP_CAN1_TX_IRQHandler(void)
{
    UsbCdcDevice_IrqHandler();
    UsbMscDevice_IrqHandler();
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    UsbCdcDevice_IrqHandler();
    UsbMscDevice_IrqHandler();
}

void USBWakeUp_IRQHandler(void)
{
    UsbCdcDevice_IrqHandler();
    UsbMscDevice_IrqHandler();
}
