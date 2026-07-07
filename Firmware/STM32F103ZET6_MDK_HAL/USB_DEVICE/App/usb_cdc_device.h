#ifndef USB_CDC_DEVICE_H
#define USB_CDC_DEVICE_H

#include "main.h"

extern volatile uint32_t g_usb_cdc_diag[16];

void UsbCdcDevice_IrqHandler(void);

#endif
