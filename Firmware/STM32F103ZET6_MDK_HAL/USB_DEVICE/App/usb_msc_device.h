#ifndef USB_MSC_DEVICE_H
#define USB_MSC_DEVICE_H

#include "main.h"

int UsbMscDevice_Start(void);
void UsbMscDevice_IrqHandler(void);

#endif
