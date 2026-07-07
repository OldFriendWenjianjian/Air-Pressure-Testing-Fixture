#include "app_config.h"
#include "app_jlink_uart_control.h"
#include "main.h"
#include "usb_cdc_device.h"
#include "usb_msc_device.h"

#define APP_FAULT_RECORD_SIGNATURE 0x4641554Cu
#define APP_FAULT_TYPE_HARD        1u
#define APP_FAULT_TYPE_MEMMANAGE   2u
#define APP_FAULT_TYPE_BUS         3u
#define APP_FAULT_TYPE_USAGE       4u

volatile AppFaultRecord g_app_fault_record;

static void AppFault_Record(uint32_t fault_type, uint32_t *stacked_regs, uint32_t exc_return)
{
    g_app_fault_record.signature = APP_FAULT_RECORD_SIGNATURE;
    g_app_fault_record.fault_type = fault_type;
    g_app_fault_record.stacked_r0 = stacked_regs[0];
    g_app_fault_record.stacked_r1 = stacked_regs[1];
    g_app_fault_record.stacked_r2 = stacked_regs[2];
    g_app_fault_record.stacked_r3 = stacked_regs[3];
    g_app_fault_record.stacked_r12 = stacked_regs[4];
    g_app_fault_record.stacked_lr = stacked_regs[5];
    g_app_fault_record.stacked_pc = stacked_regs[6];
    g_app_fault_record.stacked_psr = stacked_regs[7];
    g_app_fault_record.cfsr = SCB->CFSR;
    g_app_fault_record.hfsr = SCB->HFSR;
    g_app_fault_record.dfsr = SCB->DFSR;
    g_app_fault_record.afsr = SCB->AFSR;
    g_app_fault_record.mmfar = SCB->MMFAR;
    g_app_fault_record.bfar = SCB->BFAR;
    g_app_fault_record.msp = __get_MSP();
    g_app_fault_record.psp = __get_PSP();
    g_app_fault_record.exc_return = exc_return;
}

__NO_RETURN static void AppFault_Loop(void)
{
    __disable_irq();
    while (1) {
    }
}

__attribute__((naked)) static void AppFault_Trap(uint32_t fault_type)
{
    __asm volatile(
        "mov r1, r0                        \n"
        "tst lr, #4                        \n"
        "ite eq                            \n"
        "mrseq r0, msp                     \n"
        "mrsne r0, psp                     \n"
        "mov r2, lr                        \n"
        "b AppFault_TrapC                  \n"
    );
}

void AppFault_TrapC(uint32_t *stacked_regs, uint32_t fault_type, uint32_t exc_return)
{
    AppFault_Record(fault_type, stacked_regs, exc_return);
    AppFault_Loop();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    AppFault_Trap(APP_FAULT_TYPE_HARD);
}

void MemManage_Handler(void)
{
    AppFault_Trap(APP_FAULT_TYPE_MEMMANAGE);
}

void BusFault_Handler(void)
{
    AppFault_Trap(APP_FAULT_TYPE_BUS);
}

void UsageFault_Handler(void)
{
    AppFault_Trap(APP_FAULT_TYPE_USAGE);
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
