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

Minimum frame size is `11` bytes. Maximum payload for the initial firmware skeleton is `192` bytes.

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

Total payload length: `118` bytes.

The Qt architecture view consumes this full snapshot directly: valve blinking comes from `ValveOpenMask`, tank and channel pressure labels come from `Pressure001mmHg`, and PCBA connection/result badges come from the PCBA masks and `PcbaPressure001mmHg`.

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
- Command dispatch for `HELLO`, `GET_STATUS`, `START`, `STOP`, `PAUSE`, `RESUME`, `SET_STATE`, `SET_THRESHOLD`, `MANUAL_VALVE`, and `ENTER_MSC_REBOOT`.
- Periodic `STATUS_SNAPSHOT` reporting from existing valve, pressure, PCBA, and state-machine getters.

Still not included yet:

- Actual STM32 USB Device Core, CDC class, and MSC class files.
- Concrete implementations of `UsbCdcControl_Start`, `UsbCdcControl_Read`, and `UsbCdcControl_Write`.
- MSC boot persistence and reboot implementation behind `UsbCdcControl_RequestMscReboot`.
