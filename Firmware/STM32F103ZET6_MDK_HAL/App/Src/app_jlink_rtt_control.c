#include "app_config.h"
#include "app_jlink_rtt_control.h"
#include "app_usb_control.h"
#include "SEGGER_RTT.h"

#if APP_PC_LINK_JLINK_RTT_ENABLED

static uint8_t s_started = 0u;
static uint8_t s_msc_reboot_pending = 0u;
static uint8_t s_rx_line[USB_CTRL_MAX_FRAME_SIZE * 2u];
static uint16_t s_rx_line_len = 0u;
static uint8_t s_decoded_frame[USB_CTRL_MAX_FRAME_SIZE];
static uint8_t s_encoded_frame[(USB_CTRL_MAX_FRAME_SIZE * 2u) + 2u];
static uint32_t s_read_idle_loops = 0u;

#define APP_JLINK_RTT_CONTROL_BUFFER_INDEX 0u

static void rtt_write_text(const char *text)
{
    uint16_t len = 0u;

    if (text == NULL) {
        return;
    }
    while (text[len] != '\0') {
        ++len;
    }
    if (len > 0u) {
        (void)SEGGER_RTT_Write(APP_JLINK_RTT_CONTROL_BUFFER_INDEX, text, len);
    }
}

static uint8_t hex_nibble(uint8_t ch)
{
    if (ch >= (uint8_t)'0' && ch <= (uint8_t)'9') {
        return (uint8_t)(ch - (uint8_t)'0');
    }
    if (ch >= (uint8_t)'A' && ch <= (uint8_t)'F') {
        return (uint8_t)(10u + ch - (uint8_t)'A');
    }
    if (ch >= (uint8_t)'a' && ch <= (uint8_t)'f') {
        return (uint8_t)(10u + ch - (uint8_t)'a');
    }
    return 0xFFu;
}

static uint8_t is_hex_char(uint8_t ch)
{
    return hex_nibble(ch) != 0xFFu ? 1u : 0u;
}

int AppJlinkRttControl_Start(void)
{
    static const uint8_t ready_msg[] = "RTT_READY\n";

    if (s_started == 0u) {
        SEGGER_RTT_Init();
        (void)SEGGER_RTT_Write(APP_JLINK_RTT_CONTROL_BUFFER_INDEX,
                               ready_msg,
                               sizeof(ready_msg) - 1u);
        s_started = 1u;
    }
    return 0;
}

void AppJlinkRttControl_WriteText(const char *text)
{
    if (s_started == 0u) {
        (void)AppJlinkRttControl_Start();
    }
    rtt_write_text(text);
}

void AppJlinkRttControl_DebugText(const char *text)
{
#if APP_PC_LINK_JLINK_RTT_DEBUG_TEXT_ENABLED
    AppJlinkRttControl_WriteText(text);
#else
    (void)text;
#endif
}

int AppJlinkRttControl_Read(uint8_t *data, uint16_t max_len)
{
    uint8_t chunk[64];
    int read_len;
    uint16_t i;

    if (data == NULL || max_len == 0u) {
        return 0;
    }
    if (s_started == 0u) {
        (void)AppJlinkRttControl_Start();
    }

    do {
        read_len = (int)SEGGER_RTT_Read(APP_JLINK_RTT_CONTROL_BUFFER_INDEX, chunk, sizeof(chunk));
        if (read_len > 0) {
            s_read_idle_loops = 0u;
            AppJlinkRttControl_DebugText("RTT_READ_BYTES\n");
        } else {
            ++s_read_idle_loops;
            if (s_read_idle_loops >= 200u) {
                AppJlinkRttControl_DebugText("RTT_READ_IDLE\n");
                s_read_idle_loops = 0u;
            }
        }
        for (i = 0u; i < (uint16_t)read_len; ++i) {
            uint8_t ch = chunk[i];
            if (ch == (uint8_t)'\r') {
                continue;
            }
            if (ch == (uint8_t)'\n') {
                AppJlinkRttControl_DebugText("RTT_RX_NL\n");
                static const uint8_t rx_ok_msg[] = "RTT_RX_OK\n";
                uint16_t decoded_len = 0u;
                uint16_t pos = 0u;

                if ((s_rx_line_len >= 2u) &&
                    ((s_rx_line_len & 1u) == 0u) &&
                    (s_rx_line_len <= (uint16_t)(USB_CTRL_MAX_FRAME_SIZE * 2u))) {
                    while (pos < s_rx_line_len && decoded_len < USB_CTRL_MAX_FRAME_SIZE) {
                        uint8_t hi = hex_nibble(s_rx_line[pos]);
                        uint8_t lo = hex_nibble(s_rx_line[pos + 1u]);
                        if (hi == 0xFFu || lo == 0xFFu) {
                            decoded_len = 0u;
                            break;
                        }
                        s_decoded_frame[decoded_len++] = (uint8_t)((hi << 4) | lo);
                        pos = (uint16_t)(pos + 2u);
                    }
                    if (decoded_len > 0u) {
                        if (decoded_len == 13u) {
                            AppJlinkRttControl_DebugText("RTT_RX_LEN13\n");
                        } else {
                            AppJlinkRttControl_DebugText("RTT_RX_LEN_OTHER\n");
                        }
                        AppJlinkRttControl_DebugText((const char *)rx_ok_msg);
                        if (decoded_len > max_len) {
                            decoded_len = max_len;
                        }
                        for (i = 0u; i < decoded_len; ++i) {
                            data[i] = s_decoded_frame[i];
                        }
                        s_rx_line_len = 0u;
                        return (int)decoded_len;
                    }
                }
                AppJlinkRttControl_DebugText("RTT_RX_DROP\n");
                s_rx_line_len = 0u;
                continue;
            }

            if (is_hex_char(ch) == 0u) {
                s_rx_line_len = 0u;
                continue;
            }
            if (s_rx_line_len >= sizeof(s_rx_line)) {
                s_rx_line_len = 0u;
                continue;
            }
            s_rx_line[s_rx_line_len++] = ch;
        }
    } while (read_len > 0);

    return 0;
}

int AppJlinkRttControl_Write(const uint8_t *data, uint16_t len)
{
    static const uint8_t hex_chars[] = "0123456789ABCDEF";
    uint16_t i;
    uint16_t encoded_len;

    if (data == NULL || len == 0u) {
        return 0;
    }
    if (s_started == 0u) {
        (void)AppJlinkRttControl_Start();
    }
    if (len > USB_CTRL_MAX_FRAME_SIZE) {
        return 0;
    }

    encoded_len = (uint16_t)(len * 2u);
    for (i = 0u; i < len; ++i) {
        s_encoded_frame[i * 2u] = hex_chars[(data[i] >> 4) & 0x0Fu];
        s_encoded_frame[(i * 2u) + 1u] = hex_chars[data[i] & 0x0Fu];
    }
    s_encoded_frame[encoded_len++] = (uint8_t)'\n';
    return (int)SEGGER_RTT_Write(APP_JLINK_RTT_CONTROL_BUFFER_INDEX, s_encoded_frame, encoded_len);
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

#else

int AppJlinkRttControl_Start(void)
{
    return 0;
}

int AppJlinkRttControl_Read(uint8_t *data, uint16_t max_len)
{
    (void)data;
    (void)max_len;
    return 0;
}

int AppJlinkRttControl_Write(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return 0;
}

void AppJlinkRttControl_WriteText(const char *text)
{
    (void)text;
}

void AppJlinkRttControl_DebugText(const char *text)
{
    (void)text;
}

void AppJlinkRttControl_RequestMscReboot(void)
{
}

uint8_t AppJlinkRttControl_IsMscRebootPending(void)
{
    return 0u;
}

#endif
