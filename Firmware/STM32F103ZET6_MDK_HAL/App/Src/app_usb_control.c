#include "app_usb_control.h"
#include "app_adc_calibration.h"
#include "app_config.h"
#include "app_current.h"
#include "app_jlink_rtt_control.h"
#include "app_power.h"
#include "app_pressure.h"
#include "app_rtc.h"
#include "app_valves.h"
#include "main.h"

typedef struct {
    uint8_t boot_mode;
    uint8_t rx_buf[USB_CTRL_MAX_FRAME_SIZE * 2u];
    uint16_t rx_len;
    uint16_t report_sequence;
    uint32_t last_report_ms;
    uint8_t last_error;
} AppUsbControlContext;

static AppUsbControlContext s_usb_ctrl;
static uint8_t s_task_rx_chunk[64];
static uint8_t s_status_payload[USB_CTRL_STATUS_SNAPSHOT_LEN];
static uint8_t s_tx_frame[USB_CTRL_MAX_FRAME_SIZE];
#if APP_PC_LINK_JLINK_RTT_ENABLED
static uint32_t s_usb_task_alive_ms = 0u;
#endif

static void collect_status_snapshot(uint16_t sequence, UsbCtrlStatusSnapshot *snapshot);
static void send_ack(uint16_t sequence, uint8_t accepted_command, uint8_t status_code);
static void send_nak(uint16_t sequence, uint8_t rejected_command, uint8_t error_code);
static void send_status(uint8_t frame_type, uint16_t sequence);
static void send_pcba_timing(uint16_t sequence);
static void send_single_tank_pcba(uint16_t sequence);
static void handle_frame(const UsbCtrlFrame *frame);
static uint8_t usb_control_write_all(const uint8_t *data, uint16_t len);

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t get_i32_le(const uint8_t *p)
{
    return (int32_t)get_u32_le(p);
}

static uint32_t current_ua_to_x100(float current_ua)
{
    if (current_ua <= 0.0f) {
        return 0u;
    }
    if (current_ua >= 42949672.0f) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)((current_ua * 100.0f) + 0.5f);
}

uint16_t UsbCtrl_Crc16Modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == 0 && len > 0u) {
        return 0u;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

size_t UsbCtrl_BuildFrame(uint8_t type,
                          uint16_t sequence,
                          uint8_t command,
                          const uint8_t *payload,
                          uint16_t payload_len,
                          uint8_t *out,
                          size_t out_size)
{
    size_t frame_len = USB_CTRL_MIN_FRAME_SIZE + payload_len;

    if (out == 0 || payload_len > USB_CTRL_MAX_PAYLOAD || frame_len > out_size) {
        return 0u;
    }
    if (payload_len > 0u && payload == 0) {
        return 0u;
    }

    out[0] = USB_CTRL_FRAME_HEAD0;
    out[1] = USB_CTRL_FRAME_HEAD1;
    out[2] = USB_CTRL_PROTOCOL_VERSION;
    out[3] = type;
    put_u16_le(&out[4], sequence);
    out[6] = command;
    put_u16_le(&out[7], payload_len);

    for (uint16_t i = 0; i < payload_len; ++i) {
        out[9u + i] = payload[i];
    }

    uint16_t crc = UsbCtrl_Crc16Modbus(&out[2], (size_t)(7u + payload_len));
    put_u16_le(&out[9u + payload_len], crc);

    return frame_len;
}

size_t UsbCtrl_BuildAck(uint16_t sequence,
                        uint8_t accepted_command,
                        uint8_t status_code,
                        uint8_t *out,
                        size_t out_size)
{
    uint8_t payload[2];

    payload[0] = accepted_command;
    payload[1] = status_code;
    return UsbCtrl_BuildFrame(USB_CTRL_FRAME_RESPONSE,
                              sequence,
                              USB_CTRL_CMD_ACK,
                              payload,
                              sizeof(payload),
                              out,
                              out_size);
}

size_t UsbCtrl_BuildNak(uint16_t sequence,
                        uint8_t rejected_command,
                        uint8_t error_code,
                        uint8_t *out,
                        size_t out_size)
{
    uint8_t payload[2];

    payload[0] = rejected_command;
    payload[1] = error_code;
    return UsbCtrl_BuildFrame(USB_CTRL_FRAME_RESPONSE,
                              sequence,
                              USB_CTRL_CMD_NAK,
                              payload,
                              sizeof(payload),
                              out,
                              out_size);
}

size_t UsbCtrl_BuildStatusSnapshot(uint16_t sequence,
                                   const UsbCtrlStatusSnapshot *snapshot,
                                   uint8_t *out,
                                   size_t out_size)
{
    uint8_t payload[USB_CTRL_STATUS_SNAPSHOT_LEN];
    uint16_t payload_len = 0u;

    if (!UsbCtrl_PackStatusSnapshot(snapshot, payload, &payload_len)) {
        return 0u;
    }

    return UsbCtrl_BuildFrame(USB_CTRL_FRAME_REPORT,
                              sequence,
                              USB_CTRL_CMD_STATUS_SNAPSHOT,
                              payload,
                              payload_len,
                              out,
                              out_size);
}

UsbCtrlParseResult UsbCtrl_ParseFrame(const uint8_t *bytes,
                                      size_t len,
                                      UsbCtrlFrame *frame,
                                      size_t *consumed)
{
    uint16_t payload_len;
    size_t expected_len;
    uint16_t rx_crc;
    uint16_t calc_crc;

    if (consumed != 0) {
        *consumed = 0u;
    }
    if (bytes == 0 || frame == 0) {
        return USB_CTRL_PARSE_BAD_ARG;
    }
    if (len < 2u) {
        return USB_CTRL_PARSE_NEED_MORE;
    }
    if (bytes[0] != USB_CTRL_FRAME_HEAD0 || bytes[1] != USB_CTRL_FRAME_HEAD1) {
        if (consumed != 0) {
            *consumed = 1u;
        }
        return USB_CTRL_PARSE_BAD_HEAD;
    }
    if (len < USB_CTRL_MIN_FRAME_SIZE) {
        return USB_CTRL_PARSE_NEED_MORE;
    }
    if (bytes[2] != USB_CTRL_PROTOCOL_VERSION) {
        if (consumed != 0) {
            *consumed = USB_CTRL_MIN_FRAME_SIZE;
        }
        return USB_CTRL_PARSE_BAD_VERSION;
    }

    payload_len = get_u16_le(&bytes[7]);
    expected_len = USB_CTRL_MIN_FRAME_SIZE + payload_len;
    if (payload_len > USB_CTRL_MAX_PAYLOAD) {
        if (consumed != 0) {
            *consumed = USB_CTRL_MIN_FRAME_SIZE;
        }
        return USB_CTRL_PARSE_BAD_LENGTH;
    }
    if (len < expected_len) {
        return USB_CTRL_PARSE_NEED_MORE;
    }

    rx_crc = get_u16_le(&bytes[9u + payload_len]);
    calc_crc = UsbCtrl_Crc16Modbus(&bytes[2], (size_t)(7u + payload_len));
    if (rx_crc != calc_crc) {
        if (consumed != 0) {
            *consumed = expected_len;
        }
        return USB_CTRL_PARSE_BAD_CRC;
    }

    frame->version = bytes[2];
    frame->type = bytes[3];
    frame->sequence = get_u16_le(&bytes[4]);
    frame->command = bytes[6];
    frame->length = payload_len;
    for (uint16_t i = 0; i < payload_len; ++i) {
        frame->payload[i] = bytes[9u + i];
    }

    if (consumed != 0) {
        *consumed = expected_len;
    }
    return USB_CTRL_PARSE_OK;
}

bool UsbCtrl_PackStatusSnapshot(const UsbCtrlStatusSnapshot *snapshot,
                                uint8_t *payload,
                                uint16_t *payload_len)
{
    if (snapshot == 0 || payload == 0 || payload_len == 0) {
        return false;
    }

    put_u32_le(&payload[0], snapshot->uptime_ms);
    payload[4] = snapshot->boot_mode;
    payload[5] = snapshot->runtime_state;
    payload[6] = snapshot->workflow_flags;
    payload[7] = snapshot->error_code;
    put_u16_le(&payload[8], snapshot->target_hold_pressure_mmhg);
    put_u16_le(&payload[10], snapshot->pressure_tolerance_001mmhg);
    put_u32_le(&payload[12], snapshot->valve_open_mask);
    put_u32_le(&payload[16], snapshot->elapsed_in_state_ms);
    put_u16_le(&payload[20], snapshot->snapshot_counter);
    put_u16_le(&payload[22], snapshot->pcba_online_mask);
    put_u16_le(&payload[24], snapshot->pcba_low_power_ok_mask);
    put_u16_le(&payload[26], snapshot->pcba_normal_power_ok_mask);
    put_u16_le(&payload[28], snapshot->pcba_pass_mask);

    for (uint8_t i = 0u; i < USB_CTRL_PRESSURE_SENSOR_COUNT; ++i) {
        put_u32_le(&payload[30u + ((uint16_t)i * 4u)], snapshot->pressure_001mmhg[i]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        put_u32_le(&payload[86u + ((uint16_t)i * 4u)], snapshot->pcba_pressure_001mmhg[i]);
    }
    put_u32_le(&payload[118], snapshot->pressure_valid_mask);
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        put_u32_le(&payload[122u + ((uint16_t)i * 4u)], snapshot->pcba_standby_current_ua_x100[i]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        put_u32_le(&payload[154u + ((uint16_t)i * 4u)], snapshot->pcba_work_current_ua_x100[i]);
    }
    put_u16_le(&payload[186], snapshot->pcba_standby_current_valid_mask);
    put_u16_le(&payload[188], snapshot->pcba_work_current_valid_mask);
    put_u32_le(&payload[190], snapshot->rtc_epoch_seconds);
    payload[194] = snapshot->rtc_flags;
    payload[195] = snapshot->pcba_current_flags;
    put_u16_le(&payload[196], snapshot->adc_vrefint_raw);
    put_u16_le(&payload[198], snapshot->adc_vdda_mv);
    put_u32_le(&payload[200], snapshot->adc_scale_ppm);
    payload[204] = snapshot->adc_calibration_flags;
    put_u16_le(&payload[205], snapshot->pressure_fault_latched_mask);
    payload[207] = snapshot->pcba_power_flags;
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        put_u16_le(&payload[208u + ((uint16_t)i * 2u)], snapshot->pcba_current_raw_adc[i]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PRESSURE_SENSOR_COUNT; ++i) {
        payload[224u + i] = snapshot->pressure_status_byte[i];
        payload[238u + i] = snapshot->pressure_fault_code[i];
    }

    *payload_len = USB_CTRL_STATUS_SNAPSHOT_LEN;
    return true;
}

bool UsbCtrl_UnpackStatusSnapshot(const uint8_t *payload,
                                  uint16_t payload_len,
                                  UsbCtrlStatusSnapshot *snapshot)
{
    if (payload == 0 || snapshot == 0 || payload_len != USB_CTRL_STATUS_SNAPSHOT_LEN) {
        return false;
    }

    snapshot->uptime_ms = get_u32_le(&payload[0]);
    snapshot->boot_mode = payload[4];
    snapshot->runtime_state = payload[5];
    snapshot->workflow_flags = payload[6];
    snapshot->error_code = payload[7];
    snapshot->target_hold_pressure_mmhg = get_u16_le(&payload[8]);
    snapshot->pressure_tolerance_001mmhg = get_u16_le(&payload[10]);
    snapshot->valve_open_mask = get_u32_le(&payload[12]);
    snapshot->elapsed_in_state_ms = get_u32_le(&payload[16]);
    snapshot->snapshot_counter = get_u16_le(&payload[20]);
    snapshot->pcba_online_mask = get_u16_le(&payload[22]);
    snapshot->pcba_low_power_ok_mask = get_u16_le(&payload[24]);
    snapshot->pcba_normal_power_ok_mask = get_u16_le(&payload[26]);
    snapshot->pcba_pass_mask = get_u16_le(&payload[28]);

    for (uint8_t i = 0u; i < USB_CTRL_PRESSURE_SENSOR_COUNT; ++i) {
        snapshot->pressure_001mmhg[i] = get_u32_le(&payload[30u + ((uint16_t)i * 4u)]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        snapshot->pcba_pressure_001mmhg[i] = get_u32_le(&payload[86u + ((uint16_t)i * 4u)]);
    }
    snapshot->pressure_valid_mask = get_u32_le(&payload[118]);
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        snapshot->pcba_standby_current_ua_x100[i] = get_u32_le(&payload[122u + ((uint16_t)i * 4u)]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        snapshot->pcba_work_current_ua_x100[i] = get_u32_le(&payload[154u + ((uint16_t)i * 4u)]);
    }
    snapshot->pcba_standby_current_valid_mask = get_u16_le(&payload[186]);
    snapshot->pcba_work_current_valid_mask = get_u16_le(&payload[188]);
    snapshot->rtc_epoch_seconds = get_u32_le(&payload[190]);
    snapshot->rtc_flags = payload[194];
    snapshot->pcba_current_flags = payload[195];
    snapshot->adc_vrefint_raw = get_u16_le(&payload[196]);
    snapshot->adc_vdda_mv = get_u16_le(&payload[198]);
    snapshot->adc_scale_ppm = get_u32_le(&payload[200]);
    snapshot->adc_calibration_flags = payload[204];
    snapshot->pressure_fault_latched_mask = get_u16_le(&payload[205]);
    snapshot->pcba_power_flags = payload[207];
    for (uint8_t i = 0u; i < USB_CTRL_PCBA_CHANNEL_COUNT; ++i) {
        snapshot->pcba_current_raw_adc[i] = get_u16_le(&payload[208u + ((uint16_t)i * 2u)]);
    }
    for (uint8_t i = 0u; i < USB_CTRL_PRESSURE_SENSOR_COUNT; ++i) {
        snapshot->pressure_status_byte[i] = payload[224u + i];
        snapshot->pressure_fault_code[i] = payload[238u + i];
    }

    return true;
}

void AppUsbControl_Init(AppBootMode boot_mode)
{
    s_usb_ctrl.boot_mode = boot_mode == APP_MODE_USB_MSC ?
                           USB_CTRL_BOOT_USB_MSC :
                           USB_CTRL_BOOT_NORMAL_CDC;
    s_usb_ctrl.rx_len = 0u;
    s_usb_ctrl.report_sequence = 1u;
    s_usb_ctrl.last_report_ms = 0u;
    s_usb_ctrl.last_error = USB_CTRL_ERROR_OK;

    if (boot_mode == APP_MODE_NORMAL) {
        (void)UsbCdcControl_Start();
    }
#if APP_PC_LINK_JLINK_RTT_ENABLED
    if (boot_mode == APP_MODE_USB_MSC) {
        AppJlinkRttControl_DebugText("USB_CTRL_BOOT=USB_MSC\n");
    } else {
        AppJlinkRttControl_DebugText("USB_CTRL_BOOT=NORMAL\n");
    }
#endif
}

void AppUsbControl_OnRxBytes(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u) {
        return;
    }

    for (uint16_t i = 0u; i < len; ++i) {
        if (s_usb_ctrl.rx_len >= sizeof(s_usb_ctrl.rx_buf)) {
            s_usb_ctrl.rx_len = 0u;
        }
        s_usb_ctrl.rx_buf[s_usb_ctrl.rx_len++] = data[i];
    }
}

static uint8_t usb_control_write_all(const uint8_t *data, uint16_t len)
{
    uint16_t written_total = 0u;
    uint32_t stalled_since;

    if (data == 0 || len == 0u) {
        return 0u;
    }

    stalled_since = HAL_GetTick();
    while (written_total < len) {
        int written = UsbCdcControl_Write(&data[written_total], (uint16_t)(len - written_total));
        if (written > 0) {
            written_total = (uint16_t)(written_total + (uint16_t)written);
            stalled_since = HAL_GetTick();
            continue;
        }
        if ((HAL_GetTick() - stalled_since) > 100u) {
            return 0u;
        }
        HAL_Delay(1u);
    }

    return 1u;
}

void AppUsbControl_Task(void)
{
    int read_len;
    UsbCtrlFrame frame;
    size_t consumed;
    UsbCtrlParseResult parse_result;
    uint32_t now;

    if (s_usb_ctrl.boot_mode == USB_CTRL_BOOT_USB_MSC &&
        APP_PC_LINK_JLINK_RTT_ENABLED == 0) {
        return;
    }

#if APP_PC_LINK_JLINK_RTT_ENABLED
    now = HAL_GetTick();
    if ((now - s_usb_task_alive_ms) >= 1000u) {
        s_usb_task_alive_ms = now;
        AppJlinkRttControl_DebugText("USB_TASK_ALIVE\n");
    }
#endif

    do {
        read_len = UsbCdcControl_Read(s_task_rx_chunk, sizeof(s_task_rx_chunk));
        if (read_len > 0) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("USB_RX_CHUNK\n");
#endif
            AppUsbControl_OnRxBytes(s_task_rx_chunk, (uint16_t)read_len);
        }
    } while (read_len > 0);

    while (s_usb_ctrl.rx_len > 0u) {
        consumed = 0u;
        parse_result = UsbCtrl_ParseFrame(s_usb_ctrl.rx_buf, s_usb_ctrl.rx_len, &frame, &consumed);
        if (parse_result == USB_CTRL_PARSE_NEED_MORE) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            if (s_usb_ctrl.rx_len < USB_CTRL_MIN_FRAME_SIZE) {
                AppJlinkRttControl_DebugText("USB_NEED_HEAD\n");
            } else {
                const uint16_t payload_len = get_u16_le(&s_usb_ctrl.rx_buf[7]);
                if (payload_len > USB_CTRL_MAX_PAYLOAD) {
                    AppJlinkRttControl_DebugText("USB_NEED_BAD_PL\n");
                } else {
                    const uint16_t expected_len = (uint16_t)(USB_CTRL_MIN_FRAME_SIZE + payload_len);
                    if (s_usb_ctrl.rx_len < expected_len) {
                        AppJlinkRttControl_DebugText("USB_NEED_MORE\n");
                    } else {
                        AppJlinkRttControl_DebugText("USB_NEED_OTHER\n");
                    }
                }
            }
#endif
            break;
        }
        if (parse_result == USB_CTRL_PARSE_OK) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("USB_PARSE_OK\n");
#endif
            handle_frame(&frame);
        } else if (parse_result == USB_CTRL_PARSE_BAD_CRC) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("USB_PARSE_BAD_CRC\n");
#endif
            s_usb_ctrl.last_error = USB_CTRL_ERROR_CRC;
        } else if (parse_result == USB_CTRL_PARSE_BAD_VERSION) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("USB_PARSE_BAD_VER\n");
#endif
            s_usb_ctrl.last_error = USB_CTRL_ERROR_VERSION;
        } else {
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("USB_PARSE_BAD_LEN\n");
#endif
            s_usb_ctrl.last_error = USB_CTRL_ERROR_BAD_LENGTH;
        }

        if (consumed == 0u || consumed > s_usb_ctrl.rx_len) {
            consumed = 1u;
        }
        for (uint16_t i = 0u; i < (uint16_t)(s_usb_ctrl.rx_len - consumed); ++i) {
            s_usb_ctrl.rx_buf[i] = s_usb_ctrl.rx_buf[i + consumed];
        }
        s_usb_ctrl.rx_len = (uint16_t)(s_usb_ctrl.rx_len - consumed);
    }

    now = HAL_GetTick();
    if ((now - s_usb_ctrl.last_report_ms) >= APP_PC_LINK_STATUS_PERIOD_MS) {
        s_usb_ctrl.last_report_ms = now;
        send_status(USB_CTRL_FRAME_REPORT, s_usb_ctrl.report_sequence++);
    }
}

int AppUsbControl_BuildCurrentStatus(uint8_t frame_type,
                                     uint16_t sequence,
                                     uint8_t *out,
                                     uint16_t out_size)
{
    uint16_t payload_len = 0u;
    UsbCtrlStatusSnapshot snapshot;

    collect_status_snapshot(sequence, &snapshot);
    if (!UsbCtrl_PackStatusSnapshot(&snapshot, s_status_payload, &payload_len)) {
        return 0;
    }

    return (int)UsbCtrl_BuildFrame(frame_type,
                                  sequence,
                                  USB_CTRL_CMD_STATUS_SNAPSHOT,
                                  s_status_payload,
                                  payload_len,
                                  out,
                                  out_size);
}

static void send_ack(uint16_t sequence, uint8_t accepted_command, uint8_t status_code)
{
    size_t len = UsbCtrl_BuildAck(sequence,
                                  accepted_command,
                                  status_code,
                                  s_tx_frame,
                                  sizeof(s_tx_frame));
    if (len > 0u) {
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void send_nak(uint16_t sequence, uint8_t rejected_command, uint8_t error_code)
{
    size_t len;

    s_usb_ctrl.last_error = error_code;
    len = UsbCtrl_BuildNak(sequence,
                           rejected_command,
                           error_code,
                           s_tx_frame,
                           sizeof(s_tx_frame));
    if (len > 0u) {
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void collect_status_snapshot(uint16_t sequence, UsbCtrlStatusSnapshot *snapshot)
{
    uint32_t valve_mask = 0u;
    uint16_t online_mask = 0u;
    uint16_t low_mask = 0u;
    uint16_t normal_mask = 0u;
    uint16_t pass_mask = 0u;
    uint16_t standby_current_valid_mask = 0u;
    uint16_t work_current_valid_mask = 0u;
    uint32_t pressure_valid_mask = 0u;
    uint32_t tolerance = AppStateMachine_GetPressureTolerance001mmHg();
    uint32_t target_hold_pressure_mmhg = USB_CTRL_DEFAULT_HOLD_PRESSURE_MMHG;

    if (snapshot == 0) {
        return;
    }

    for (uint8_t valve = 1u; valve <= 26u; ++valve) {
        if (AppValves_IsOpen(valve) != 0u) {
            valve_mask |= (uint32_t)1u << (valve - 1u);
        }
    }

    for (uint8_t ch = 0u; ch < APP_PCBA_CHANNEL_COUNT; ++ch) {
        uint32_t pcba_pressure = AppStateMachine_GetPcbaTestPressure001mmHg(ch);
        uint32_t fixture_pressure = AppPressure_Get001mmHg((PressureSensorIndex)(PRESSURE_SENSOR_CH1 + ch));
        uint32_t delta = pcba_pressure > fixture_pressure ?
                         pcba_pressure - fixture_pressure :
                         fixture_pressure - pcba_pressure;

        if (AppStateMachine_IsPcbaOnline(ch) != 0u) {
            online_mask |= (uint16_t)(1u << ch);
        }
        if (AppStateMachine_IsPcbaLowPowerOk(ch) != 0u) {
            low_mask |= (uint16_t)(1u << ch);
        }
        if (AppStateMachine_IsPcbaNormalPowerOk(ch) != 0u) {
            normal_mask |= (uint16_t)(1u << ch);
        }
        if (pcba_pressure != 0u && delta <= tolerance) {
            pass_mask |= (uint16_t)(1u << ch);
        }
        snapshot->pcba_pressure_001mmhg[ch] = pcba_pressure;
        snapshot->pcba_standby_current_ua_x100[ch] =
            current_ua_to_x100(AppCurrent_GetStandbyUa((uint8_t)(ch + 1u)));
        snapshot->pcba_work_current_ua_x100[ch] =
            current_ua_to_x100(AppCurrent_GetWorkUa((uint8_t)(ch + 1u)));
        snapshot->pcba_current_raw_adc[ch] = AppCurrent_GetRaw((uint8_t)(ch + 1u));
        if (AppCurrent_IsStandbyValid((uint8_t)(ch + 1u)) != 0u) {
            standby_current_valid_mask |= (uint16_t)(1u << ch);
        }
        if (AppCurrent_IsWorkValid((uint8_t)(ch + 1u)) != 0u) {
            work_current_valid_mask |= (uint16_t)(1u << ch);
        }
    }

    snapshot->uptime_ms = HAL_GetTick();
    snapshot->boot_mode = UsbCdcControl_IsMscRebootPending() != 0u ?
                          USB_CTRL_BOOT_MSC_REBOOT_PENDING :
                          s_usb_ctrl.boot_mode;
    snapshot->runtime_state = (uint8_t)AppStateMachine_GetState();
    snapshot->workflow_flags = 0u;
    snapshot->workflow_flags |= AppStateMachine_IsRunning() != 0u ? USB_CTRL_WORKFLOW_RUNNING : 0u;
    snapshot->workflow_flags |= AppStateMachine_IsPaused() != 0u ? USB_CTRL_WORKFLOW_PAUSED : 0u;
    snapshot->workflow_flags |= AppStateMachine_IsError() != 0u ? USB_CTRL_WORKFLOW_ERROR : 0u;
    snapshot->workflow_flags |= AppStateMachine_IsManualMode() != 0u ? USB_CTRL_WORKFLOW_MANUAL : 0u;
    snapshot->workflow_flags |= AppStateMachine_IsSinglePcbaFlowActive() != 0u ? USB_CTRL_WORKFLOW_SINGLE_PCBA : 0u;
    if (AppStateMachine_IsSingleTankLoopActive() != 0u) {
        target_hold_pressure_mmhg =
            AppStateMachine_GetSingleTankTarget001mmHg() / APP_PRESSURE_SCALE_PER_MMHG;
        tolerance = AppStateMachine_GetSingleTankTolerance001mmHg();
    }
    snapshot->error_code = s_usb_ctrl.last_error;
    snapshot->target_hold_pressure_mmhg =
        (uint16_t)(target_hold_pressure_mmhg > 0xFFFFu ? 0xFFFFu : target_hold_pressure_mmhg);
    snapshot->pressure_tolerance_001mmhg = (uint16_t)(tolerance > 0xFFFFu ? 0xFFFFu : tolerance);
    snapshot->valve_open_mask = valve_mask;
    snapshot->elapsed_in_state_ms = AppStateMachine_GetStateElapsedMs();
    snapshot->snapshot_counter = sequence;
    snapshot->pcba_online_mask = online_mask;
    snapshot->pcba_low_power_ok_mask = low_mask;
    snapshot->pcba_normal_power_ok_mask = normal_mask;
    snapshot->pcba_pass_mask = pass_mask;
    snapshot->pcba_standby_current_valid_mask = standby_current_valid_mask;
    snapshot->pcba_work_current_valid_mask = work_current_valid_mask;
    snapshot->rtc_epoch_seconds = AppRtc_GetEpochSeconds();
    snapshot->rtc_flags = AppRtc_GetFlags();
    snapshot->pcba_current_flags = (AppStateMachine_GetState() == APP_STATE_PCBA_CURRENT_TEST &&
                                    AppPower_Is50mATestCircuitEnabled() != 0u) ? 0x01u : 0u;
    snapshot->pcba_power_flags = 0u;
    snapshot->pcba_power_flags |= AppPower_Is5VEnabled() != 0u ? 0x01u : 0u;
    snapshot->pcba_power_flags |= AppPower_Is45VEnabled() != 0u ? 0x02u : 0u;
    snapshot->adc_vrefint_raw = AppAdcCalibration_GetVrefintRaw();
    snapshot->adc_vdda_mv = (uint16_t)AppAdcCalibration_GetVddaMv();
    snapshot->adc_scale_ppm = AppAdcCalibration_GetScalePpm();
    snapshot->adc_calibration_flags = AppAdcCalibration_GetFlags();
    snapshot->pressure_fault_latched_mask =
        (uint16_t)(AppPressure_GetFaultLatchedMask() & 0x3FFFu);

    for (uint8_t i = 0u; i < USB_CTRL_PRESSURE_SENSOR_COUNT; ++i) {
        if (AppPressure_IsValid((PressureSensorIndex)i) != 0) {
            pressure_valid_mask |= (uint32_t)1u << i;
        }
        snapshot->pressure_001mmhg[i] = AppPressure_Get001mmHg((PressureSensorIndex)i);
        snapshot->pressure_status_byte[i] = AppPressure_GetStatusByte((PressureSensorIndex)i);
        snapshot->pressure_fault_code[i] = AppPressure_GetFaultCode((PressureSensorIndex)i);
    }
    snapshot->pressure_valid_mask = pressure_valid_mask;
}

static void send_status(uint8_t frame_type, uint16_t sequence)
{
    UsbCtrlStatusSnapshot snapshot;
    uint16_t payload_len = 0u;
    size_t len;

    collect_status_snapshot(sequence, &snapshot);
    if (!UsbCtrl_PackStatusSnapshot(&snapshot, s_status_payload, &payload_len)) {
        return;
    }

    len = UsbCtrl_BuildFrame(frame_type,
                             sequence,
                             USB_CTRL_CMD_STATUS_SNAPSHOT,
                             s_status_payload,
                             payload_len,
                             s_tx_frame,
                             sizeof(s_tx_frame));
    if (len > 0u) {
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void send_pcba_timing(uint16_t sequence)
{
    AppPcbaTimingReport report;
    uint8_t payload[4u + (APP_PCBA_TIMING_STEP_COUNT * 40u)];
    uint16_t payload_len = 4u;

    AppStateMachine_GetPcbaTimingReport(&report);
    payload[0] = report.running;
    payload[1] = report.done;
    payload[2] = report.count;
    payload[3] = report.final_result;

    for (uint8_t i = 0u; i < report.count && i < APP_PCBA_TIMING_STEP_COUNT; ++i) {
        uint16_t base = (uint16_t)(4u + (i * 40u));
        payload[base + 0u] = report.entries[i].kind;
        payload[base + 1u] = report.entries[i].cmd_sent;
        payload[base + 2u] = report.entries[i].ok;
        payload[base + 3u] = report.entries[i].resp_cmd_or_byte;
        payload[base + 4u] = report.entries[i].resp_channel;
        payload[base + 5u] = report.entries[i].resp_len;
        payload[base + 6u] = report.entries[i].resp_data0;
        payload[base + 7u] = report.entries[i].resp_data1;
        payload[base + 8u] = report.entries[i].resp_data2;
        payload[base + 9u] = report.entries[i].resp_data3;
        payload[base + 10u] = report.entries[i].raw_len;
        payload[base + 11u] = 0u;
        put_u32_le(&payload[base + 12u], report.entries[i].elapsed_us);
        for (uint8_t raw_i = 0u; raw_i < APP_PCBA_REPORT_RAW_MAX; ++raw_i) {
            payload[base + 16u + raw_i] = raw_i < report.entries[i].raw_len ? report.entries[i].raw[raw_i] : 0u;
        }
        payload_len = (uint16_t)(base + 40u);
    }

    size_t len = UsbCtrl_BuildFrame(USB_CTRL_FRAME_RESPONSE,
                                    sequence,
                                    USB_CTRL_CMD_GET_PCBA_TIMING,
                                    payload,
                                    payload_len,
                                    s_tx_frame,
                                    sizeof(s_tx_frame));
    if (len > 0u) {
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void send_single_tank_pcba(uint16_t sequence)
{
    AppSingleTankPcbaReport report;
    uint8_t payload[12u + (APP_SINGLE_TANK_PCBA_STEP_COUNT * 52u)];
    uint16_t payload_len = 12u;

    AppStateMachine_GetSingleTankPcbaReport(&report);
    payload[0] = report.running;
    payload[1] = report.done;
    payload[2] = report.count;
    payload[3] = report.final_result;
    put_u32_le(&payload[4], report.standby_current_ua_x100);
    put_u32_le(&payload[8], report.work_current_ua_x100);

    for (uint8_t i = 0u; i < report.count && i < APP_SINGLE_TANK_PCBA_STEP_COUNT; ++i) {
        uint16_t base = (uint16_t)(12u + (i * 52u));
        payload[base + 0u] = report.entries[i].kind;
        payload[base + 1u] = report.entries[i].cmd_sent;
        payload[base + 2u] = report.entries[i].ok;
        payload[base + 3u] = report.entries[i].flags;
        payload[base + 4u] = report.entries[i].resp_cmd_or_byte;
        payload[base + 5u] = report.entries[i].resp_channel;
        payload[base + 6u] = report.entries[i].resp_len;
        payload[base + 7u] = report.entries[i].resp_data0;
        payload[base + 8u] = report.entries[i].resp_data1;
        payload[base + 9u] = report.entries[i].resp_data2;
        payload[base + 10u] = report.entries[i].resp_data3;
        payload[base + 11u] = 0u;
        put_u32_le(&payload[base + 12u], report.entries[i].current_ua_x100);
        put_u32_le(&payload[base + 16u], report.entries[i].elapsed_us);
        put_u32_le(&payload[base + 20u], report.entries[i].parsed_value);
        payload[base + 24u] = report.entries[i].raw_len;
        payload[base + 25u] = 0u;
        payload[base + 26u] = 0u;
        payload[base + 27u] = 0u;
        for (uint8_t raw_i = 0u; raw_i < APP_PCBA_REPORT_RAW_MAX; ++raw_i) {
            payload[base + 28u + raw_i] = raw_i < report.entries[i].raw_len ? report.entries[i].raw[raw_i] : 0u;
        }
        payload_len = (uint16_t)(base + 52u);
    }

    size_t len = UsbCtrl_BuildFrame(USB_CTRL_FRAME_RESPONSE,
                                    sequence,
                                    USB_CTRL_CMD_GET_SINGLE_TANK_PCBA,
                                    payload,
                                    payload_len,
                                    s_tx_frame,
                                    sizeof(s_tx_frame));
    if (len > 0u) {
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void handle_hello(const UsbCtrlFrame *frame)
{
    uint8_t payload[6];
    uint8_t capability = 0x03u;
    size_t len;

#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("HELLO_HANDLER\n");
#endif

    if (frame->length != 2u || frame->payload[0] != USB_CTRL_PROTOCOL_VERSION) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
        AppJlinkRttControl_DebugText("HELLO_BAD\n");
#endif
        send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_VERSION);
        return;
    }

    payload[0] = USB_CTRL_PROTOCOL_VERSION;
    payload[1] = capability;
    put_u16_le(&payload[2], USB_CTRL_MAX_PAYLOAD);
    put_u16_le(&payload[4], APP_PC_LINK_STATUS_PERIOD_MS);
    len = UsbCtrl_BuildFrame(USB_CTRL_FRAME_RESPONSE,
                             frame->sequence,
                             USB_CTRL_CMD_HELLO,
                             payload,
                             sizeof(payload),
                             s_tx_frame,
                             sizeof(s_tx_frame));
    if (len > 0u) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
        AppJlinkRttControl_DebugText("HELLO_TX\n");
#endif
        (void)usb_control_write_all(s_tx_frame, (uint16_t)len);
    }
}

static void handle_frame(const UsbCtrlFrame *frame)
{
    int rc = -1;

    if (frame == 0 || frame->type != USB_CTRL_FRAME_REQUEST) {
        return;
    }

    switch (frame->command) {
    case USB_CTRL_CMD_HELLO:
        handle_hello(frame);
        return;

    case USB_CTRL_CMD_GET_STATUS:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        send_status(USB_CTRL_FRAME_RESPONSE, frame->sequence);
        return;

    case USB_CTRL_CMD_START:
        if (frame->length != 3u || get_u16_le(frame->payload) > USB_CTRL_DEFAULT_HOLD_PRESSURE_MMHG) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        rc = AppStateMachine_RequestStart();
        break;

    case USB_CTRL_CMD_STOP:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppStateMachine_RequestStop();
        break;

    case USB_CTRL_CMD_PAUSE:
        rc = frame->length == 0u ? AppStateMachine_RequestPause() : -1;
        break;

    case USB_CTRL_CMD_RESUME:
        rc = frame->length == 0u ? AppStateMachine_RequestResume() : -1;
        break;

    case USB_CTRL_CMD_SET_STATE:
        if (frame->length != 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppStateMachine_RequestState((AppRuntimeState)frame->payload[0]);
        break;

    case USB_CTRL_CMD_SET_THRESHOLD:
        if (frame->length != 5u || frame->payload[0] != 0x02u || get_i32_le(&frame->payload[1]) < 0) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        AppStateMachine_SetPressureTolerance001mmHg((uint32_t)get_i32_le(&frame->payload[1]));
        rc = 0;
        break;

    case USB_CTRL_CMD_MANUAL_VALVE:
        if (frame->length != 4u || frame->payload[0] < 1u || frame->payload[0] > 26u || frame->payload[1] > 2u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        AppStateMachine_SetManualValve(frame->payload[0], frame->payload[1] == 0u ? 0u : 1u);
        rc = 0;
        break;

    case USB_CTRL_CMD_ENTER_MSC_REBOOT:
        if (frame->length != 4u ||
            frame->payload[0] != 'M' ||
            frame->payload[1] != 'S' ||
            frame->payload[2] != 'C' ||
            frame->payload[3] != '!') {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        (void)AppStateMachine_RequestStop();
        UsbCdcControl_RequestMscReboot();
        rc = 0;
        break;

    case USB_CTRL_CMD_SET_RTC_TIME:
        if (frame->length != 4u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppRtc_SetEpochSeconds(get_u32_le(frame->payload));
        if (rc == 0) {
            (void)AppStateMachine_RequestState(APP_STATE_RTC_DEBUG);
        }
        break;

    case USB_CTRL_CMD_SET_PCBA_CURRENT_RANGE:
        if (frame->length != 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        if (frame->payload[0] > 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        AppStateMachine_SetPcbaCurrent50mAEnabled(frame->payload[0]);
        rc = 0;
        break;

    case USB_CTRL_CMD_CALIBRATE_ADC:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppAdcCalibration_Refresh();
        break;

    case USB_CTRL_CMD_SET_PCBA_SUPPLY_VOLTAGE:
        if (frame->length != 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        if (frame->payload[0] != 45u && frame->payload[0] != 50u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        AppStateMachine_SetPcbaSupply5VEnabled(frame->payload[0] == 50u ? 1u : 0u);
        rc = 0;
        break;

    case USB_CTRL_CMD_SET_VALVE_MASK: {
        const uint32_t valve_mask = get_u32_le(frame->payload);
        const uint32_t open_mask = get_u32_le(&frame->payload[4]);
        if (frame->length != 8u ||
            (valve_mask & 0xFC000000u) != 0u ||
            (open_mask & ~valve_mask) != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        AppStateMachine_SetManualValveMask(valve_mask, open_mask);
        rc = 0;
        break;
    }

    case USB_CTRL_CMD_SINGLE_TANK_LOOP:
        if (frame->length != 10u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        if (frame->payload[0] >= APP_TANK_COUNT) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        if (frame->payload[9] > 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_VALUE);
            return;
        }
        if (frame->payload[9] != 0u) {
            rc = AppStateMachine_RequestSingleTankLoop(frame->payload[0],
                                                       get_u32_le(&frame->payload[1]),
                                                       get_u32_le(&frame->payload[5]));
        } else {
            rc = AppStateMachine_StopSingleTankLoop();
        }
        break;

    case USB_CTRL_CMD_RUN_PCBA_TIMING:
        if (frame->length > 1u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppStateMachine_RequestPcbaTimingDiagnostic(frame->length > 0u ? frame->payload[0] : 0u);
        break;

    case USB_CTRL_CMD_GET_PCBA_TIMING:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        send_pcba_timing(frame->sequence);
        return;

    case USB_CTRL_CMD_RUN_SINGLE_TANK_PCBA:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        rc = AppStateMachine_RequestSingleTankPcbaDiagnostic();
        break;

    case USB_CTRL_CMD_GET_SINGLE_TANK_PCBA:
        if (frame->length != 0u) {
            send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_LENGTH);
            return;
        }
        send_single_tank_pcba(frame->sequence);
        return;

    default:
        send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_UNSUPPORTED);
        return;
    }

    if (rc == 0) {
        s_usb_ctrl.last_error = USB_CTRL_ERROR_OK;
        send_ack(frame->sequence, frame->command, USB_CTRL_ERROR_OK);
        send_status(USB_CTRL_FRAME_REPORT, s_usb_ctrl.report_sequence++);
    } else {
        send_nak(frame->sequence, frame->command, USB_CTRL_ERROR_BAD_STATE);
    }
}

__WEAK int UsbCdcControl_Start(void)
{
    return 0;
}

__WEAK int UsbCdcControl_Read(uint8_t *data, uint16_t max_len)
{
    (void)data;
    (void)max_len;
    return 0;
}

__WEAK int UsbCdcControl_Write(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return 0;
}

__WEAK void UsbCdcControl_RequestMscReboot(void)
{
}

__WEAK uint8_t UsbCdcControl_IsMscRebootPending(void)
{
    return 0u;
}
