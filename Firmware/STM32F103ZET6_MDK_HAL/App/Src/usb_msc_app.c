#include "usb_msc_app.h"
#include "bsp_w25q128.h"
#include "usb_msc_device.h"

int UsbMscApp_Start(void)
{
    if (W25Q128_Init() != 0) {
        return -1;
    }

    return UsbMscDevice_Start();
}

void UsbMscApp_Task(void)
{
}
