#ifndef APP_USB_CONTROL_H
#define APP_USB_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state_machine.h"

#define USB_CTRL_FRAME_HEAD0                    0xA5u
#define USB_CTRL_FRAME_HEAD1                    0x5Au
#define USB_CTRL_PROTOCOL_VERSION               0x01u
#define USB_CTRL_MAX_PAYLOAD                    1024u
#define USB_CTRL_MIN_FRAME_SIZE                 11u
#define USB_CTRL_MAX_FRAME_SIZE                 (USB_CTRL_MIN_FRAME_SIZE + USB_CTRL_MAX_PAYLOAD)
#define USB_CTRL_DEFAULT_HOLD_PRESSURE_MMHG     285u
#define USB_CTRL_PRESSURE_SENSOR_COUNT          14u
#define USB_CTRL_PCBA_CHANNEL_COUNT             8u
#define USB_CTRL_STATUS_SNAPSHOT_LEN            252u

typedef enum {
    USB_CTRL_FRAME_REQUEST  = 0x01u,
    USB_CTRL_FRAME_RESPONSE = 0x02u,
    USB_CTRL_FRAME_REPORT   = 0x03u
} UsbCtrlFrameType;

typedef enum {
    USB_CTRL_CMD_HELLO             = 0x01u,
    USB_CTRL_CMD_GET_STATUS        = 0x02u,
    USB_CTRL_CMD_START             = 0x03u,
    USB_CTRL_CMD_STOP              = 0x04u,
    USB_CTRL_CMD_PAUSE             = 0x05u,
    USB_CTRL_CMD_RESUME            = 0x06u,
    USB_CTRL_CMD_SET_STATE         = 0x07u,
    USB_CTRL_CMD_SET_THRESHOLD     = 0x08u,
    USB_CTRL_CMD_MANUAL_VALVE      = 0x09u,
    USB_CTRL_CMD_ENTER_MSC_REBOOT  = 0x0Au,
    USB_CTRL_CMD_SET_RTC_TIME      = 0x0Bu,
    USB_CTRL_CMD_SET_PCBA_CURRENT_RANGE = 0x0Cu,
    USB_CTRL_CMD_CALIBRATE_ADC     = 0x0Du,
    USB_CTRL_CMD_SET_VALVE_MASK    = 0x0Eu,
    USB_CTRL_CMD_SINGLE_TANK_LOOP  = 0x0Fu,
    USB_CTRL_CMD_RUN_PCBA_TIMING   = 0x10u,
    USB_CTRL_CMD_GET_PCBA_TIMING   = 0x11u,
    USB_CTRL_CMD_RUN_SINGLE_TANK_PCBA = 0x12u,
    USB_CTRL_CMD_GET_SINGLE_TANK_PCBA = 0x13u,
    USB_CTRL_CMD_SET_PCBA_SUPPLY_VOLTAGE = 0x14u,
    USB_CTRL_CMD_STATUS_SNAPSHOT   = 0x7Eu,
    USB_CTRL_CMD_ACK               = 0x7Fu,
    USB_CTRL_CMD_NAK               = 0x80u
} UsbCtrlCommand;

typedef enum {
    USB_CTRL_PARSE_OK = 0,
    USB_CTRL_PARSE_NEED_MORE,
    USB_CTRL_PARSE_BAD_ARG,
    USB_CTRL_PARSE_BAD_HEAD,
    USB_CTRL_PARSE_BAD_VERSION,
    USB_CTRL_PARSE_BAD_LENGTH,
    USB_CTRL_PARSE_BAD_CRC
} UsbCtrlParseResult;

typedef enum {
    USB_CTRL_BOOT_NORMAL_CDC = 0,
    USB_CTRL_BOOT_USB_MSC,
    USB_CTRL_BOOT_MSC_REBOOT_PENDING
} UsbCtrlBootMode;

typedef enum {
    USB_CTRL_WORKFLOW_RUNNING = 0x01u,
    USB_CTRL_WORKFLOW_PAUSED  = 0x02u,
    USB_CTRL_WORKFLOW_ERROR   = 0x04u,
    USB_CTRL_WORKFLOW_MANUAL  = 0x08u,
    USB_CTRL_WORKFLOW_SINGLE_PCBA = 0x10u
} UsbCtrlWorkflowFlag;

typedef enum {
    USB_CTRL_ERROR_OK = 0,
    USB_CTRL_ERROR_BAD_LENGTH = 0x01u,
    USB_CTRL_ERROR_BAD_STATE = 0x02u,
    USB_CTRL_ERROR_BAD_VALUE = 0x03u,
    USB_CTRL_ERROR_BUSY = 0x04u,
    USB_CTRL_ERROR_UNSUPPORTED = 0x05u,
    USB_CTRL_ERROR_CRC = 0x06u,
    USB_CTRL_ERROR_VERSION = 0x07u
} UsbCtrlErrorCode;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t sequence;
    uint8_t command;
    uint16_t length;
    uint8_t payload[USB_CTRL_MAX_PAYLOAD];
} UsbCtrlFrame;

typedef struct {
    uint32_t uptime_ms;
    uint8_t boot_mode;
    uint8_t runtime_state;
    uint8_t workflow_flags;
    uint8_t error_code;
    uint16_t target_hold_pressure_mmhg;
    uint16_t pressure_tolerance_001mmhg;
    uint32_t valve_open_mask;
    uint32_t elapsed_in_state_ms;
    uint16_t snapshot_counter;
    uint16_t pcba_online_mask;
    uint16_t pcba_low_power_ok_mask;
    uint16_t pcba_normal_power_ok_mask;
    uint16_t pcba_pass_mask;
    uint32_t pressure_001mmhg[USB_CTRL_PRESSURE_SENSOR_COUNT];
    uint32_t pcba_pressure_001mmhg[USB_CTRL_PCBA_CHANNEL_COUNT];
    uint32_t pressure_valid_mask;
    uint32_t pcba_standby_current_ua_x100[USB_CTRL_PCBA_CHANNEL_COUNT];
    uint32_t pcba_work_current_ua_x100[USB_CTRL_PCBA_CHANNEL_COUNT];
    uint16_t pcba_standby_current_valid_mask;
    uint16_t pcba_work_current_valid_mask;
    uint32_t rtc_epoch_seconds;
    uint8_t rtc_flags;
    uint8_t pcba_current_flags;
    uint16_t adc_vrefint_raw;
    uint16_t adc_vdda_mv;
    uint32_t adc_scale_ppm;
    uint8_t adc_calibration_flags;
    uint16_t pressure_fault_latched_mask;
    uint8_t pcba_power_flags;
    uint16_t pcba_current_raw_adc[USB_CTRL_PCBA_CHANNEL_COUNT];
    uint8_t pressure_status_byte[USB_CTRL_PRESSURE_SENSOR_COUNT];
    uint8_t pressure_fault_code[USB_CTRL_PRESSURE_SENSOR_COUNT];
} UsbCtrlStatusSnapshot;

uint16_t UsbCtrl_Crc16Modbus(const uint8_t *data, size_t len);

size_t UsbCtrl_BuildFrame(uint8_t type,
                          uint16_t sequence,
                          uint8_t command,
                          const uint8_t *payload,
                          uint16_t payload_len,
                          uint8_t *out,
                          size_t out_size);

size_t UsbCtrl_BuildAck(uint16_t sequence,
                        uint8_t accepted_command,
                        uint8_t status_code,
                        uint8_t *out,
                        size_t out_size);

size_t UsbCtrl_BuildNak(uint16_t sequence,
                        uint8_t rejected_command,
                        uint8_t error_code,
                        uint8_t *out,
                        size_t out_size);

size_t UsbCtrl_BuildStatusSnapshot(uint16_t sequence,
                                   const UsbCtrlStatusSnapshot *snapshot,
                                   uint8_t *out,
                                   size_t out_size);

UsbCtrlParseResult UsbCtrl_ParseFrame(const uint8_t *bytes,
                                      size_t len,
                                      UsbCtrlFrame *frame,
                                      size_t *consumed);

bool UsbCtrl_PackStatusSnapshot(const UsbCtrlStatusSnapshot *snapshot,
                                uint8_t *payload,
                                uint16_t *payload_len);

bool UsbCtrl_UnpackStatusSnapshot(const uint8_t *payload,
                                  uint16_t payload_len,
                                  UsbCtrlStatusSnapshot *snapshot);

void AppUsbControl_Init(AppBootMode boot_mode);
void AppUsbControl_Task(void);
void AppUsbControl_OnRxBytes(const uint8_t *data, uint16_t len);
int AppUsbControl_BuildCurrentStatus(uint8_t frame_type,
                                     uint16_t sequence,
                                     uint8_t *out,
                                     uint16_t out_size);

int UsbCdcControl_Start(void);
int UsbCdcControl_Read(uint8_t *data, uint16_t max_len);
int UsbCdcControl_Write(const uint8_t *data, uint16_t len);
void UsbCdcControl_RequestMscReboot(void);
uint8_t UsbCdcControl_IsMscRebootPending(void);

#endif
