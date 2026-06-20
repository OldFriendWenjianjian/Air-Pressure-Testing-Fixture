# PC-MCU USB Control Protocol

## 1. Scope

This document defines the lightweight USB control protocol between the Qt PC application and the STM32 MCU fixture firmware.

- Normal mode: USB CDC virtual COM port.
- Maintenance mode: long press `KEY1` at boot to keep the existing USB MSC U disk mode.
- Maximum hold pressure controlled by the PC workflow: `285mmHg`.
- This protocol describes the byte frame, command payloads, and MCU status snapshots only. It does not change the existing firmware state machine or USB stack.

## 2. Roles

### Qt PC application

- Opens the CDC virtual COM port.
- Sends `HELLO` after port open and checks protocol compatibility.
- Sends workflow controls: `START`, `STOP`, `PAUSE`, `RESUME`, `SET_STATE`, `SET_THRESHOLD`, `MANUAL_VALVE`, `ENTER_MSC_REBOOT`.
- Displays MCU periodic status snapshots.
- Shows whether the MCU is in normal CDC mode or preparing for MSC reboot.

### MCU firmware

- Parses frames received from CDC.
- Replies with `ACK` or `NAK` for command frames.
- Periodically reports `STATUS_SNAPSHOT`.
- Keeps long-press `KEY1` MSC boot behavior unchanged.
- Enters MSC by accepting `ENTER_MSC_REBOOT`, acknowledging it, persisting the requested boot mode if supported by platform code, then rebooting. The persistence and reboot hook are not part of this skeleton.
- Current repository status: the firmware business layer is wired to `AppUsbControl_Task()` and builds into the Keil image. The actual STM32 USB Device Core plus CDC/MSC class implementation is still intentionally isolated behind `UsbCdcControl_*` and `MX_USB_DEVICE_Init()` hooks.

## 3. Transport

- Physical/logical link: USB CDC ACM virtual serial port in normal mode.
- Byte order: little endian for all multi-byte integers.
- Character strings: ASCII, no terminating zero unless explicitly stated.
- Frame boundary: detected by fixed two-byte frame head and payload length.
- Recommended MCU report period: `200ms` to `500ms`; the PC should tolerate at least `2s` without a snapshot before showing link stale.

## 4. Frame Format

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Head0 | `0xA5` |
| 1 | 1 | Head1 | `0x5A` |
| 2 | 1 | Version | Current `0x01` |
| 3 | 1 | Type | See frame types |
| 4 | 2 | Sequence | Sender-owned sequence number |
| 6 | 1 | Command | See command IDs |
| 7 | 2 | Length | Payload length, little endian |
| 9 | N | Payload | Command-specific payload |
| 9 + N | 2 | CRC16 | CRC16/MODBUS, little endian |

CRC input starts at `Version` and ends at the final payload byte. The frame head and CRC bytes are not included in the CRC calculation.

Minimum frame size is `11` bytes. Maximum payload for the current firmware is `256` bytes.

## 5. Frame Types

| Value | Name | Direction | Description |
|---:|---|---|---|
| `0x01` | `REQUEST` | PC to MCU | Command request |
| `0x02` | `RESPONSE` | MCU to PC | Command response |
| `0x03` | `REPORT` | MCU to PC | Periodic or asynchronous report |

## 6. Commands

| Value | Name | Type | Payload |
|---:|---|---|---|
| `0x01` | `HELLO` | Request/Response | Protocol handshake |
| `0x02` | `GET_STATUS` | Request/Response | Immediate status snapshot |
| `0x03` | `START` | Request/Response | Start automatic workflow |
| `0x04` | `STOP` | Request/Response | Stop workflow and enter safe stopped state |
| `0x05` | `PAUSE` | Request/Response | Pause workflow |
| `0x06` | `RESUME` | Request/Response | Resume paused workflow |
| `0x07` | `SET_STATE` | Request/Response | Request a runtime state |
| `0x08` | `SET_THRESHOLD` | Request/Response | Configure pressure/current thresholds |
| `0x09` | `MANUAL_VALVE` | Request/Response | Manually open or close one valve |
| `0x0A` | `ENTER_MSC_REBOOT` | Request/Response | Reboot into USB MSC maintenance mode |
| `0x0B` | `SET_RTC_TIME` | Request/Response | Set MCU RTC epoch seconds |
| `0x0C` | `SET_PCBA_CURRENT_RANGE` | Request/Response | Select PCBA current debug range |
| `0x0D` | `CALIBRATE_ADC` | Request/Response | Refresh internal VREFINT reference immediately |
| `0x7E` | `STATUS_SNAPSHOT` | Report/Response | MCU status snapshot |
| `0x7F` | `ACK` | Response | Generic success |
| `0x80` | `NAK` | Response | Generic failure |

The MCU should echo the request sequence in the corresponding response. `STATUS_SNAPSHOT` reports use the MCU's own sequence counter.

## 7. Common Response Payloads

### ACK payload

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | AcceptedCommand | Command ID being acknowledged |
| 1 | 1 | StatusCode | `0` success; non-zero warning code |

### NAK payload

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | RejectedCommand | Command ID being rejected |
| 1 | 1 | ErrorCode | See error codes |

### Error codes

| Value | Name | Description |
|---:|---|---|
| `0x01` | `BAD_LENGTH` | Payload length does not match command |
| `0x02` | `BAD_STATE` | Command is not allowed in current state |
| `0x03` | `BAD_VALUE` | Payload value is out of range |
| `0x04` | `BUSY` | MCU cannot accept the command now |
| `0x05` | `UNSUPPORTED` | Command or parameter is unsupported |
| `0x06` | `CRC_ERROR` | Frame CRC check failed |
| `0x07` | `VERSION_MISMATCH` | Protocol version is not supported |

## 8. Command Payloads

### HELLO

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | PcProtocolVersion | Expected `1` |
| 1 | 1 | PcCapability | Bit mask, reserved for future |

MCU response payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | McuProtocolVersion | Current `1` |
| 1 | 1 | McuCapability | Bit0: CDC control supported; Bit1: MSC reboot request supported |
| 2 | 2 | MaxPayload | Maximum payload bytes accepted by MCU |
| 4 | 2 | SnapshotPeriodMs | Current periodic snapshot interval |

### GET_STATUS

Request payload is empty. MCU responds with `STATUS_SNAPSHOT` using the request sequence.

### START

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 2 | TargetHoldPressureMmHg | Recommended `285`; MCU must reject values above `285` unless firmware policy explicitly allows service override |
| 2 | 1 | StartMode | `0` normal automatic workflow; other values reserved |

### STOP, PAUSE, RESUME

Request payload is empty. MCU responds with `ACK` or `NAK`.

### SET_STATE

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | RuntimeState | Existing firmware runtime state number |

This command is intended for supervised Qt operator control and debug workflows. The MCU remains responsible for rejecting unsafe transitions.

### SET_THRESHOLD

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | ThresholdId | See threshold IDs |
| 1 | 4 | Value | Signed 32-bit little endian value |

Threshold IDs:

| Value | Name | Unit |
|---:|---|---|
| `0x01` | `HOLD_PRESSURE_MAX` | `mmHg`; default and maximum normal request `285` |
| `0x02` | `PRESSURE_TOLERANCE` | `0.001mmHg` |
| `0x03` | `HOLD_TIME` | `ms` |
| `0x04` | `CURRENT_MIN` | `uA` |
| `0x05` | `CURRENT_MAX` | `uA` |

### MANUAL_VALVE

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | ValveNumber | Existing firmware valve number |
| 1 | 1 | Action | `0` close; `1` open; `2` pulse |
| 2 | 2 | PulseMs | Pulse duration for action `2`, otherwise `0` |

The MCU should reject manual valve control while an unsafe automatic state is active.

### ENTER_MSC_REBOOT

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `MSC!`: bytes `0x4D 0x53 0x43 0x21` |

Expected MCU behavior:

1. Validate magic.
2. Send `ACK`.
3. Close valves and put outputs into a safe state.
4. Request next boot into USB MSC maintenance mode if platform code supports it.
5. Reboot.

Long press `KEY1` remains the primary no-PC MSC entry path.

### SET_RTC_TIME

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | EpochSeconds | Unix epoch seconds, little endian |

The MCU writes this value into the STM32F1 RTC counter and enters `RTC时钟调试模式` when accepted. The Qt RTC debug panel sends the PC current time or the operator-edited time using this command.

### SET_PCBA_CURRENT_RANGE

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Enable50mA | `0` means uA mode and PB1 keeps the shared low-resistance branch off; `1` means mA mode and PB1 turns the shared low-resistance branch on |

This command controls only the PCBA current debug mode range. The Qt PCBA current test panel sends it after entering `PCBA电流测试` and whenever the operator toggles the PB1 shared low-resistance branch control.

The hardware is shared by all 8 PCBA current channels:

- PB1 low: the 2300N NMOS is off, only the `1kR` high-value sampling resistor is active, and the MCU reports standby/uA current through `PcbaStandbyCurrentUaX100`.
- PB1 high: the 2300N NMOS is on and enables the low-resistance branch for all 8 PCBA current channels, and the MCU reports work/mA current through `PcbaWorkCurrentUaX100`.
- The analog front-end gain is `100x`. The mA-mode effective shunt used by firmware is `1kR || (0.2R + Rds(on) of the shared 2300N NMOS)`. The `Rds(on)` value must be set from the actual BOM/datasheet at the PB1 gate-drive voltage.

### CALIBRATE_ADC

Request payload is empty. The MCU samples the STM32 internal voltage reference (`VREFINT`), calculates the current ADC reference voltage used by the board, and reports the result in the next `STATUS_SNAPSHOT`.

The firmware also refreshes this reference before every multi-channel PCBA current capture, so this command is only a manual "refresh now" debug action. No ADC reference record is saved to internal Flash. This realtime reference affects only ADC-based PCBA current conversion. The fixture pressure sensors are digital MPRLS devices read over I2C and do not use this coefficient.

## 9. STATUS_SNAPSHOT Payload

| Offset | Size | Field | Unit/Description |
|---:|---:|---|---|
| 0 | 4 | UptimeMs | MCU uptime in ms |
| 4 | 1 | BootMode | `0` normal CDC; `1` USB MSC; `2` MSC reboot pending |
| 5 | 1 | RuntimeState | Existing firmware runtime state number |
| 6 | 1 | WorkflowFlags | Bit0 running; Bit1 paused; Bit2 error; Bit3 manual mode |
| 7 | 1 | ErrorCode | Last protocol/workflow error |
| 8 | 2 | TargetHoldPressureMmHg | Current target, normally `285` |
| 10 | 2 | PressureTolerance001mmHg | Result threshold in `0.001mmHg`, for example `3000` means `3.000mmHg` |
| 12 | 4 | ValveOpenMask | Bit0 means valve 1 is open |
| 16 | 4 | ElapsedInStateMs | Current state elapsed time |
| 20 | 2 | SnapshotCounter | Increments per snapshot |
| 22 | 2 | PcbaOnlineMask | Bit0 means channel 1 PCBA is online |
| 24 | 2 | PcbaLowPowerOkMask | Bit0 means channel 1 low-voltage query passed |
| 26 | 2 | PcbaNormalPowerOkMask | Bit0 means channel 1 normal-voltage query passed |
| 28 | 2 | PcbaPassMask | Bit0 means channel 1 final result passed |
| 30 | 56 | Pressure001mmHg[14] | Pressure sensor 1 through pressure sensor 14, each `uint32` in `0.001mmHg` |
| 86 | 32 | PcbaPressure001mmHg[8] | PCBA channel 1 through channel 8 measured pressure, each `uint32` in `0.001mmHg` |
| 118 | 4 | PressureValidMask | Bit0 means pressure sensor 1 is connected and has a valid sample |
| 122 | 32 | PcbaStandbyCurrentUaX100[8] | PCBA channel 1 through channel 8 standby current, each `uint32` in `0.01uA` |
| 154 | 32 | PcbaWorkCurrentUaX100[8] | PCBA channel 1 through channel 8 current/realtime work current, each `uint32` in `0.01uA` |
| 186 | 2 | PcbaStandbyCurrentValidMask | Bit0 means channel 1 standby current has a valid captured sample |
| 188 | 2 | PcbaWorkCurrentValidMask | Bit0 means channel 1 work/realtime current has a valid captured sample |
| 190 | 4 | RtcEpochSeconds | Current MCU RTC Unix epoch seconds |
| 194 | 1 | RtcFlags | Bit0 initialized; Bit1 LSE oscillator ready; Bit2 backup-domain magic valid |
| 195 | 1 | PcbaCurrentFlags | Bit0 means PCBA current debug mode is in mA range and PB1 shared low-resistance branch is on |
| 196 | 2 | AdcVrefintRaw | Latest averaged ADC raw value for internal VREFINT |
| 198 | 2 | AdcVddaMv | Latest realtime ADC reference voltage in mV; defaults to `3300` before a valid refresh |
| 200 | 4 | AdcScalePpm | Realtime ADC scale coefficient in ppm, equal to `AdcVddaMv / 3300mV * 1000000` |
| 204 | 1 | AdcReferenceFlags | Bit0 realtime VREFINT valid; Bit1 VREFINT/VDDA range error |
| 205 | 3 | Reserved | Reserved, currently `0` |
| 208 | 16 | PcbaCurrentRawAdc[8] | PCBA current ADC raw code for channel 1 through channel 8, each `uint16`; this raw code is not corrected by `AdcScalePpm` |

Total payload length: `224` bytes. The Qt decoder remains backward compatible with older `118`, `190`, `196`, and `208` byte snapshots.

The Qt architecture view consumes this full snapshot directly: valve blinking comes from `ValveOpenMask`, tank and channel pressure labels come from `Pressure001mmHg` plus `PressureValidMask`, PCBA connection/result badges come from the PCBA masks and `PcbaPressure001mmHg`, the PCBA current debug page comes from the current arrays plus current valid masks, `PcbaCurrentFlags`, and `PcbaCurrentRawAdc`, the ADC realtime reference page comes from `Adc*` fields, and the RTC debug page comes from `RtcEpochSeconds` plus `RtcFlags`. If a pressure sensor or current valid bit is clear, the PC must display `--` instead of showing a fake value or treating `0` as a real reading.

`PcbaCurrentFlags` bit0 selects which current array the Qt PCBA current debug page should treat as the current display. When bit0 is clear, current is shown in uA from `PcbaStandbyCurrentUaX100` and `PcbaStandbyCurrentValidMask`. When bit0 is set, current is shown in mA from `PcbaWorkCurrentUaX100` and `PcbaWorkCurrentValidMask`. The debug page displays only the currently selected range, not separate standby/work columns. The current value is corrected by the realtime internal-reference coefficient; `PcbaCurrentRawAdc` remains the uncorrected ADC raw code. The same page also shows the current internal-reference correction coefficient from `AdcScalePpm / 1000000`; this coefficient is refreshed in firmware before every multi-channel PCBA current capture and is not saved to Flash.

`PcbaWorkCurrentValidMask` is set after the MCU has actually entered `PCBA电流测试` or a normal workflow work-current measurement state and captured ADC samples. Therefore the Qt host must switch the MCU to `PCBA电流测试` and send `SET_PCBA_CURRENT_RANGE` before expecting realtime current values in this debug page.

`RtcFlags` bit2 means the backup register magic was already present at RTC init time or remains present after setting the clock. This is used as the software indication that the backup domain kept data, which is the available firmware-side signal for whether the coin cell/backup supply is effectively preserving the RTC domain.

The current firmware business layer populates this snapshot from the existing valve, pressure, PCBA, and state-machine getter APIs before packing a report frame. The remaining gap is the concrete USB Device CDC transport that carries these frames to the PC.

## 10. Parser Rules

- Drop bytes until `0xA5 0x5A` is found.
- Reject unsupported version.
- Reject payload length above MCU maximum.
- Reject CRC mismatch.
- A valid parser may return "need more bytes" when a partial frame is received.
- The PC should retry a command if no response arrives within its command timeout. Recommended initial timeout: `500ms`.

## 11. Firmware Integration Notes

The firmware file `app_usb_control.c` provides:

- CRC16/MODBUS calculation.
- Frame build helpers.
- Single-frame parse helper.
- Status snapshot structure.
- Snapshot payload pack/unpack helpers.
- Command and type definitions.
- A normal-mode USB control task that consumes CDC bytes through the weak `UsbCdcControl_*` port functions.
- Command dispatch for `HELLO`, `GET_STATUS`, `START`, `STOP`, `PAUSE`, `RESUME`, `SET_STATE`, `SET_THRESHOLD`, `MANUAL_VALVE`, `ENTER_MSC_REBOOT`, `SET_RTC_TIME`, `SET_PCBA_CURRENT_RANGE`, and `CALIBRATE_ADC`.
- Periodic `STATUS_SNAPSHOT` reporting from existing valve, pressure, PCBA, and state-machine getters.

Still not included yet:

- Actual STM32 USB Device Core, CDC class, and MSC class files.
- Concrete implementations of `UsbCdcControl_Start`, `UsbCdcControl_Read`, and `UsbCdcControl_Write`.
- MSC boot persistence and reboot implementation behind `UsbCdcControl_RequestMscReboot`.
