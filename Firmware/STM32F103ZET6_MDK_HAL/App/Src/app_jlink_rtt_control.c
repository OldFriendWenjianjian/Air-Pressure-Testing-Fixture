#include "app_config.h"
#include "app_jlink_rtt_control.h"
#include "app_usb_control.h"
#include "SEGGER_RTT.h"

#if APP_PC_LINK_JLINK_RTT_ENABLED

static uint8_t s_started = 0u;
static uint8_t s_msc_reboot_pending = 0u;
static uint8_t s_up_buffer[2048];
static uint8_t s_down_buffer[512];

#define APP_JLINK_RTT_CONTROL_BUFFER_INDEX 1u

int AppJlinkRttControl_Start(void)
{
    if (s_started == 0u) {
        SEGGER_RTT_Init();
        SEGGER_RTT_ConfigUpBuffer(APP_JLINK_RTT_CONTROL_BUFFER_INDEX,
                                  "PC_CTRL_TX",
                                  s_up_buffer,
                                  sizeof(s_up_buffer),
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
        SEGGER_RTT_ConfigDownBuffer(APP_JLINK_RTT_CONTROL_BUFFER_INDEX,
                                    "PC_CTRL_RX",
                                    s_down_buffer,
                                    sizeof(s_down_buffer),
                                    SEGGER_RTT_MODE_NO_BLOCK_SKIP);
        s_started = 1u;
    }
    return 0;
}

int AppJlinkRttControl_Read(uint8_t *data, uint16_t max_len)
{
    if (data == NULL || max_len == 0u) {
        return 0;
    }
    if (s_started == 0u) {
        (void)AppJlinkRttControl_Start();
    }
    return (int)SEGGER_RTT_Read(APP_JLINK_RTT_CONTROL_BUFFER_INDEX, data, max_len);
}

int AppJlinkRttControl_Write(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0u) {
        return 0;
    }
    if (s_started == 0u) {
        (void)AppJlinkRttControl_Start();
    }
    return (int)SEGGER_RTT_Write(APP_JLINK_RTT_CONTROL_BUFFER_INDEX, data, len);
}

void AppJlinkRttControl_RequestMscReboot(void)
{
    s_msc_reboot_pending = 1u;
}

uint8_t AppJlinkRttControl_IsMscRebootPending(void)
{
    return s_msc_reboot_pending;
}

int UsbCdcControl_Start(void)
{
    return AppJlinkRttControl_Start();
}

int UsbCdcControl_Read(uint8_t *data, uint16_t max_len)
{
    return AppJlinkRttControl_Read(data, max_len);
}

int UsbCdcControl_Write(const uint8_t *data, uint16_t len)
{
    return AppJlinkRttControl_Write(data, len);
}

void UsbCdcControl_RequestMscReboot(void)
{
    AppJlinkRttControl_RequestMscReboot();
}

uint8_t UsbCdcControl_IsMscRebootPending(void)
{
    return AppJlinkRttControl_IsMscRebootPending();
}

#endif
