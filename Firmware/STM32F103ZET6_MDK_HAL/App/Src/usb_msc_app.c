#include "usb_msc_app.h"
#include "bsp_w25q128.h"

#ifndef __WEAK
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define __WEAK __weak
#else
#define __WEAK __attribute__((weak))
#endif
#endif

__WEAK void MX_USB_DEVICE_Init(void)
{
}

int UsbMscApp_Start(void)
{
    if (W25Q128_Init() != 0) {
        return -1;
    }

    MX_USB_DEVICE_Init();
    return 0;
}

void UsbMscApp_Task(void)
{
}
