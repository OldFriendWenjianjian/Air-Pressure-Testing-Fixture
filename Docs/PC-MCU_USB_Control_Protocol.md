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
- Sends workflow controls: `START`, `STOP`, `PAUSE`, `RESUME`, `SET_STATE`, `SET_THRESHOLD`, `MANUAL_VALVE`, `ENTER_MSC_REBOOT`, and sensor-calibration commands.
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
| 2 | 1 | Version | Current `0x02` |
| 3 | 1 | Type | See frame types |
| 4 | 2 | Sequence | Sender-owned sequence number |
| 6 | 1 | Command | See command IDs |
| 7 | 2 | Length | Payload length, little endian |
| 9 | N | Payload | Command-specific payload |
| 9 + N | 2 | CRC16 | CRC16/MODBUS, little endian |

CRC input starts at `Version` and ends at the final payload byte. The frame head and CRC bytes are not included in the CRC calculation.

Minimum frame size is `11` bytes. Maximum payload for the current firmware is `1100` bytes.

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
| `0x0E` | `SET_VALVE_MASK` | Request/Response | Set multiple manual valves at once |
| `0x0F` | `SINGLE_TANK_LOOP` | Request/Response | Start/stop one-tank closed-loop debug |
| `0x10` | `RUN_PCBA_TIMING` | Request/Response | Run PCBA serial timing diagnostic |
| `0x11` | `GET_PCBA_TIMING` | Request/Response | Read PCBA serial timing diagnostic report |
| `0x12` | `RUN_SINGLE_TANK_PCBA` | Request/Response | Run single-tank single-PCBA full-flow diagnostic |
| `0x13` | `GET_SINGLE_TANK_PCBA` | Request/Response/Report | Read or asynchronously receive the single-tank single-PCBA diagnostic report |
| `0x14` | `SET_PCBA_SUPPLY_VOLTAGE` | Request/Response | Select PCBA debug supply voltage |
| `0x15` | `SENSOR_CAL_ACTION` | Request/Response | Enter/exit sensor calibration, control leased valves, capture points, or update one calibration slot |
| `0x16` | `GET_SENSOR_CAL_STATUS` | Request/Response | Read the active calibration session or one saved calibration slot |
| `0x7E` | `STATUS_SNAPSHOT` | Report/Response | MCU status snapshot |
| `0x7F` | `ACK` | Response | Generic success |
| `0x80` | `NAK` | Response | Generic failure |

The MCU should echo the request sequence in the corresponding response. `STATUS_SNAPSHOT` reports use the MCU's own sequence counter.

`RUN_PCBA_TIMING` is a read-only 10-step link diagnostic. It powers the selected PCBA off for `1s`, powers it at `5V` for `1s`, then performs wake, power-on, version read, low-power query, normal-power query, and five consecutive pressure queries. It never records zero, sends calibration data, or writes PCBA Flash, and it powers the PCBA off when complete. Every GPIO software-UART route still applies the `10ms` response-start limit and continuous complete-burst capture.

While `RUN_SINGLE_TANK_PCBA` is active, the MCU publishes a `REPORT` frame with command `GET_SINGLE_TANK_PCBA` after every completed step and once more after finalization. For a serial transaction entry, a PCBA response timeout is decided by the MCU and encoded as a completed entry with `ok=0`, no raw response bytes, and the measured `ElapsedUs`; the PC must display that result and must not create its own synthetic test entry. `Kind=4` uses the same physical entry fields for pressure-trend diagnostics, so its `ElapsedUs` is the trend observation duration rather than UART response time. When `ContinueOnFail` is enabled, a failed entry remains failed and the MCU proceeds with later independent test steps unless a fixture safety fault requires termination.

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
| 0 | 1 | McuProtocolVersion | Current `2`; version 2 requires the 8-channel continuous-burst software-UART receiver, the 10ms per-route response-start limit, and the `50/150/250mmHg` single-tank calibration profile |
| 1 | 1 | McuCapability | Bit0: CDC control supported; Bit1: MSC reboot request supported; Bit2: sensor calibration supported |
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

### RUN_SINGLE_TANK_PCBA

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | ContinueOnFail | `0` stops after a failed test item; `1` records the failure and continues later test items |
| 1 | 4 | MaxTrendResidual001mmHg | Little-endian maximum absolute residual allowed between a fresh pressure sample and the fitted trend line, in `0.001mmHg`; for example, `500` means `0.50mmHg` |
| 5 | 4 | TrendWindowMs | Little-endian duration of the post-close fresh-sample trend window, in `ms` |
| 9 | 4 | MaxDropRate001mmHgPerSecond | Little-endian maximum fitted pressure decline rate, in `0.001mmHg/s`; for example, `1000` means `1.00mmHg/s` |

The current request payload is exactly 13 bytes. Implementations that retain compatibility with an empty payload or the legacy one-byte `ContinueOnFail` payload use the compiled defaults `500`, `3000ms`, and `3000`, corresponding to a `0.50mmHg` residual, a `3s` observation window, and a `3.00mmHg/s` maximum decline rate.

The Qt single-tank single-PCBA panel enables `ContinueOnFail` by default so one diagnostic run captures all calibration and validation errors. The MCU still owns all safety stops.

In this firmware, `APP_PRESSURE_SCALE_PER_MMHG` is `1000`, so these fields encode pressure at `0.001mmHg` per count.

For each V1 pressure point, the MCU may open the tank-1 inlet only once and keeps that opening continuous. The first fresh pressure-detection-1 sample that reaches the point target permanently closes the inlet for that point. The MCU must not reopen the inlet if pressure later falls, and it must not use normal relief-valve corrections to pull an overshoot back toward the target. STOP, sensor faults, and hard-overpressure handling may still force the fixture into its safe all-closed state.

After the inlet closes, the MCU fits a trend using consecutive fresh pressure-detection-1 samples collected over `TrendWindowMs`. A point qualifies only when the maximum absolute fit residual is no greater than `MaxTrendResidual001mmHg` and the fitted decline is no faster than `MaxDropRate001mmHgPerSecond`. The qualifying fresh sample selects the synchronization instant. The MCU locks the predicted pressure for that instant and immediately performs the dependent PCBA calibration or validation query; it does not refill or normally vent while waiting for the trend to qualify. A trend result and its following PCBA calibration/query result remain separate report entries.

Before `SINGLE_RECORD_ZERO_AD`, the MCU opens the tank-1 outlet, PCBA-1 channel valve, and tank-1 relief valve. Every relief-valve opening lasts at least `30s`. After pressure detection 1 enters `0.0mmHg +/- 0.1mmHg`, the valve remains continuously open for another `30s`; the MCU then closes it, waits `1s` for static equalization, and requires five fresh samples inside the zero window. Static rebound starts another complete VENT cycle. There is no normal bypass timeout: STOP or a pressure-sensor safety fault closes all valves and terminates the run without recording zero.

After the `250mmHg` calibration point and its Flash-write step, the MCU performs another VENT before starting the `100mmHg`, `200mmHg`, and `285mmHg` validation points. This inter-phase VENT must also hold pressure detection 1 within `0.0mmHg +/- 0.1mmHg` for the required consecutive fresh samples. It is a defined phase transition, not a pressure-point relief correction.

`SYNC_PRESSURE_CAL` sends the locked predicted pressure directly as a four-byte little-endian unsigned integer in `0.001mmHg`; no scale conversion is applied. For example, `49.842mmHg` is transmitted as integer `49842`. The current single-tank single-PCBA diagnostic profile uses three calibration transactions at `50mmHg`, `150mmHg`, and `250mmHg`. The number of calibration commands remains three. After each calibration transaction, the MCU immediately sends one `PRESSURE_TEST` query before changing the pneumatic state. Each query stores the MPRLS1 trend prediction for its exact query instant and compares the PCBA result against it using the configured pressure tolerance. The fixture's `295mmHg` absolute safety limit remains unchanged.

A `PRESSURE_TEST` response uses the same `0.001mmHg` unit. The MCU rejects `0xFFFFFFFF`; every other four-byte value is used directly without multiplication before comparison with the query-instant fixture prediction. All eight PCBA links are GPIO software UARTs at `115200 8N1`. Each route waits at most `10ms` for the first response start bit. Once a start bit is detected, the MCU captures the complete back-to-back response burst continuously into a per-route buffer, so the frame parser may consume individual bytes without reopening the physical receiver between bytes. A valid success response received inside the `10ms` window passes; its `ElapsedUs` records actual transaction time and is not a second pass/fail threshold. Trend-window collection occurs before the serial transaction and is reported only in the associated `Kind=4` entry, so it must not be added to UART response time. The MCU keeps resynchronizing inside the same UART window after request echoes, noise, partial frames, unexpected commands, or bad CRC frames, and returns only when the expected command and payload length have a valid CRC. The latest invalid or partial bytes are retained in the serial entry if the window expires. The MCU accepts the fixture PCBA's compatible empty success ACK and the one-byte `ACK YES` form. ACK NO, malformed responses, and pressure-query values outside tolerance remain failures and must not be displayed as no-response timeouts.

After the final PCBA shutdown transaction, or when a non-safety test failure finalizes the run, the MCU turns off PCBA power and performs the same minimum-30-second VENT, post-zero 30-second hold, static settle, and five-sample confirmation through the tank-1 path. Only then does the MCU close all valves and publish `Done=1`. STOP and pressure-sensor safety faults still close all valves immediately.

`ContinueOnFail` never bypasses fixture safety shutdowns: pressure-sensor faults and hard overpressure close the valves and finalize the run. Non-safety trend or PCBA transaction failures remain failed entries and may continue to later independent items when the option is enabled. A `STOP` request cancels the active pressure loop and leaves all valves and PCBA power outputs off.

### GET_SINGLE_TANK_PCBA

The request payload is empty. The MCU returns a 12-byte report header followed by `Count` fixed-size entries. Each current entry is exactly 52 bytes, at most 23 entries are present, and the maximum report payload is `12 + 23 * 52 = 1208` bytes.

Report header:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Running | Non-zero while the single-tank single-PCBA flow is active |
| 1 | 1 | Done | Non-zero after finalization |
| 2 | 1 | Count | Number of following 52-byte entries; maximum `23` |
| 3 | 1 | FinalResult | Non-zero only when the completed flow passes |
| 4 | 4 | StandbyCurrentUaX100 | Little-endian standby current in `0.01uA` |
| 8 | 4 | WorkCurrentUaX100 | Little-endian work current in `0.01uA` |

Entry layout, with offsets relative to the start of each 52-byte entry:

| Offset | Size | Field | General meaning |
|---:|---:|---|---|
| 0 | 1 | Kind | `0` wake byte, `1` PCBA serial transaction, `2` current sample, `3` skipped dependent step, `4` pressure trend, `5` VENT gate result |
| 1 | 1 | CommandOrFailureCode | Sent PCBA command, skipped command, or MCU pressure/VENT failure code according to `Kind` |
| 2 | 1 | Ok | Non-zero when this entry passes |
| 3 | 1 | Flags | Supply, current-range, and step flags |
| 4 | 1 | ResponseCommandOrByte | Response command/byte for serial entries; fresh trend sample count for `Kind=4` |
| 5 | 1 | ResponseChannel | PCBA response channel; unused and zero for `Kind=4` |
| 6 | 1 | ResponseLength | PCBA response data length; unused and zero for `Kind=4` |
| 7 | 4 | ResponseData[0..3] | First four PCBA response data bytes; maximum trend residual, little endian, for `Kind=4` |
| 11 | 1 | Reserved0 | Must be zero |
| 12 | 4 | CurrentUaX100 | Current sample for current entries; signed fitted slope for `Kind=4`; query-instant MPRLS1 pressure for `Kind=1`, command `0x11` |
| 16 | 4 | ElapsedUs | UART response time for serial entries; trend observation duration for `Kind=4` |
| 20 | 4 | ParsedValue | Parsed command value; predicted pressure for `Kind=4` |
| 24 | 1 | RawResponseLength | Number of valid bytes in `RawResponse`; maximum `24` |
| 25 | 3 | Reserved1 | Must be zero |
| 28 | 24 | RawResponse | Raw PCBA response bytes; unused and zero-filled for `Kind=4` |

For `Kind=4`, the reused fields have these exact meanings:

| Entry field | Encoding | Meaning |
|---|---|---|
| `ParsedValue` | `uint32`, `0.001mmHg` | Pressure predicted and locked at the selected synchronization instant |
| `CurrentUaX100` | `int32`, `0.001mmHg/s` | Signed fitted pressure slope, transported little endian through the existing four-byte unsigned slot; consumers must reinterpret the bits as two's-complement `int32`; magnitude `1000` means `1.00mmHg/s` |
| `ElapsedUs` | `uint32`, `us` | Post-close trend observation duration only; this is not UART response time |
| `ResponseCommandOrByte` | `uint8` | Number of fresh pressure-detection-1 samples used by the reported trend |
| `ResponseData[0..3]` | `uint32`, `0.001mmHg` | Maximum absolute residual of those samples from the fitted trend line, little endian; `500` means `0.50mmHg` |

For a `Kind=1`, command `0x11` pressure-query entry, `CurrentUaX100` is repurposed as an unsigned `ComparisonPressure001mmHg` field. It contains the MPRLS1 trend prediction at the exact query instant. `ParsedValue` contains the PCBA's direct `0.001mmHg` pressure value; host software subtracts these two values to display the signed error.

The predicted pressure in a successful `Kind=4` entry is used immediately by the dependent calibration or validation operation. The following serial entries independently report their PCBA responses and UART `ElapsedUs`; host software must not combine the durations.

### SET_PCBA_SUPPLY_VOLTAGE

PC request payload:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | VoltageCode | `50` means enable PCBA `5V`; `45` means enable PCBA `4.5V` |

The Qt PCBA current test panel sends this command when entering `PCBA电流测试` and whenever the operator changes the `PCBA供电电压` selector. The MCU stores the requested setting and applies it while the PCBA current debug state is active. The selected/actual supply state is reflected by `PcbaPowerFlags` in `STATUS_SNAPSHOT`.

### CALIBRATE_ADC

Request payload is empty. The MCU samples the STM32 internal voltage reference (`VREFINT`), calculates the current ADC reference voltage used by the board, and reports the result in the next `STATUS_SNAPSHOT`.

The firmware also refreshes this reference before every multi-channel PCBA current capture, so this command is only a manual "refresh now" debug action. No ADC reference record is saved to internal Flash. This realtime reference affects only ADC-based PCBA current conversion. The fixture pressure sensors are digital MPRLS devices read over I2C and do not use this coefficient.

### SENSOR_CAL_ACTION

All sensor-calibration mutations use command `0x15`. Byte 0 selects the operation. Successful requests return the normal two-byte `ACK`; rejected requests return `NAK`. The MCU owns valve timing, sample capture, profile validation, and Flash persistence.

| Operation | Value | Payload length | Payload |
|---|---:|---:|---|
| `ENTER` | `0x01` | 3 | `[Op, Mode, DestinationSlot]` |
| `EXIT` | `0x02` | 1 | `[Op]` |
| `JOG` | `0x03` | 4 | `[Op, Actuator, LeaseMs:u16]` |
| `START_AUTO_VENT` | `0x04` | 1 | `[Op]` |
| `CANCEL_AUTO_VENT` | `0x05` | 1 | `[Op]` |
| `RECORD` | `0x06` | 6 | `[Op, PointIndex, ActualPressure001mmHg:u32]` |
| `SAVE_SLOT` | `0x07` | 2 | `[Op, Slot]` |
| `CLEAR_SLOT` | `0x08` | 2 | `[Op, Slot]` |
| `RESET_SESSION` | `0x09` | 1 | `[Op]` |

`ENTER` fields:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Op | `0x01` |
| 1 | 1 | Mode | `0`: fixed-IIC1 mode; `1`: in-place mode |
| 2 | 1 | DestinationSlot | One-based pressure-sensor position, `1` through `14` |

In fixed-IIC1 mode the source is always pressure sensor 1 and the captured profile is saved only to `DestinationSlot`. In in-place mode the source and destination are both `DestinationSlot`. Entering either mode closes all valves and starts a fresh staged session. See `Sensor_Calibration_Workflow.md` for the complete pneumatic mapping.

`JOG` fields:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Op | `0x03` |
| 1 | 1 | Actuator | `0`: stop; `1`: fill; `2`: release |
| 2 | 2 | LeaseMs | Little-endian valve lease; fill/release requires a non-zero value and the MCU limits it to `500ms` |

The Qt host renews a `500ms` lease every `150ms` while the operator holds a jog button. Releasing the button sends actuator `0` immediately. Lease expiry, `EXIT`, `STOP`, USB link loss handling, overpressure, or leaving the calibration runtime state closes the calibration valves. A fill request requires a valid source path. Release and automatic VENT remain available for depressurization.

`START_AUTO_VENT` opens the mode-specific release path until every sensor used by that path remains at or below `1.000mmHg` nominal pressure for `1000ms`, or until the `30000ms` timeout expires. `CANCEL_AUTO_VENT` closes the path and clears the VENT-complete indication. VENT and `ZeroReady` are informational helpers and do not gate point 0 recording.

`RECORD` fields:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | Op | `0x06` |
| 1 | 1 | PointIndex | `0`: zero point; `1` through `3`: manually measured calibration points |
| 2 | 4 | ActualPressure001mmHg | Unsigned pressure in `0.001mmHg`; point 0 must be zero |

The MCU records the averaged raw count from the selected source; the PC never supplies a raw count. Recording is allowed only with the actuator stopped, a valid source sample, and a complete live averaging window. Raw counts and entered pressures must increase strictly by point index. The minimum adjacent raw span is `1000` counts and the minimum adjacent pressure span is `1.000mmHg`.

`SAVE_SLOT` persists the four staged points only when its one-based `Slot` equals the destination selected by `ENTER`. `CLEAR_SLOT` removes the saved profile for any slot `1` through `14`. `RESET_SESSION` clears only staged samples and does not modify saved profiles.

### GET_SENSOR_CAL_STATUS

The request is empty to read the staged session. A one-byte request containing slot `1` through `14` reads that saved slot when it exists. The response command remains `0x16` and has exactly 48 payload bytes.

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | FormatVersion | Current value `1` |
| 1 | 1 | Flags | See calibration status flags below |
| 2 | 1 | ActuatorAndMode | Bits0-1 actuator (`0` stop, `1` fill, `2` release, `3` auto VENT); bit2 in-place mode |
| 3 | 1 | CapturedMask | Bits0-3 identify captured zero/point1/point2/point3 |
| 4 | 2 | CalibratedMask | Bit0 through bit13 identify saved calibration slots 1 through 14 |
| 6 | 1 | TargetSlot | One-based session destination or requested saved slot |
| 7 | 1 | Detail | Latest detailed calibration result |
| 8 | 4 | LiveRawAverage | Current averaged source raw count |
| 12 | 4 | LiveNominalPressure001mmHg | Current source pressure before saved calibration correction |
| 16 | 8 | Point0 | Raw count and actual pressure, each `uint32` |
| 24 | 8 | Point1 | Raw count and actual pressure, each `uint32` |
| 32 | 8 | Point2 | Raw count and actual pressure, each `uint32` |
| 40 | 8 | Point3 | Raw count and actual pressure, each `uint32` |

Calibration status flags at byte 1:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | Active | Calibration runtime state is active |
| 1 | SourceValid | The selected source has a valid sample |
| 2 | SourceFault | The selected source reports a pressure fault |
| 3 | StorageLoaded | At least one valid calibration store image was loaded |
| 4 | StagedComplete | All four returned points are present |
| 5 | ZeroReady | Automatic VENT completed; point 0 recording does not depend on this flag |
| 6 | AutoVentActive | Automatic VENT is running |
| 7 | StorageFault | One or both calibration Flash images failed validation or a write failed |

Detailed result values at byte 7 are: `0` OK, `1` bad state, `2` bad value, `3` source invalid, `4` lease expired, `5` overpressure, `6` VENT complete, `7` VENT timeout, `8` storage error, `9` invalid profile, `10` invalid non-zero value for point 0, `11` captured, `12` saved, `13` cleared, and `14` not enough samples.

## 9. STATUS_SNAPSHOT Payload

| Offset | Size | Field | Unit/Description |
|---:|---:|---|---|
| 0 | 4 | UptimeMs | MCU uptime in ms |
| 4 | 1 | BootMode | `0` normal CDC; `1` USB MSC; `2` MSC reboot pending |
| 5 | 1 | RuntimeState | Existing firmware runtime state number |
| 6 | 1 | WorkflowFlags | Bit0 running; Bit1 paused; Bit2 error; Bit3 manual mode; Bit4 single-PCBA full-flow active |
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
| 205 | 2 | PressureFaultLatchedMask | Bit0 means pressure sensor 1 fault is latched |
| 207 | 1 | PcbaPowerFlags | Bit0 means PCBA `5V` enabled; Bit1 means PCBA `4.5V` enabled |
| 208 | 16 | PcbaCurrentRawAdc[8] | PCBA current ADC raw code for channel 1 through channel 8, each `uint16`; this raw code is not corrected by `AdcScalePpm` |
| 224 | 14 | PressureStatusByte[14] | Latest pressure sensor status byte |
| 238 | 14 | PressureFaultCode[14] | Latest pressure sensor fault code |
| 252 | 32 | PcbaStandbyCurrentVarianceUa2[8] | Standby-current variance for channels 1 through 8 |
| 284 | 32 | PcbaWorkCurrentVarianceUa2[8] | Work-current variance for channels 1 through 8 |
| 316 | 320 | PcbaStandbyCurrentSamplesUaX100[8][10] | Ten standby-current samples for each channel |
| 636 | 320 | PcbaWorkCurrentSamplesUaX100[8][10] | Ten work-current samples for each channel |
| 956 | 1 | SingleTankProtectionFlags | Bit0 single-tank protection active |
| 957 | 1 | SingleTankProtectionReason | MCU protection reason |
| 958 | 1 | SingleTankProtectionTankIndex | Zero-based protected tank index |
| 959 | 1 | SingleTankProtectionSensorIndex | Zero-based protected pressure-sensor index |
| 960 | 1 | SingleTankProtectionInletValve | One-based inlet valve number closed by protection |
| 961 | 2 | PressureCalibratedMask | Bit0 through bit13 mean pressure-sensor positions 1 through 14 have saved profiles |
| 963 | 1 | PressureCalibrationFlags | Bits0-1 actuator; bit2 active; bits3-6 captured mask; bit7 storage fault |
| 964 | 28 | PressureMathSaturationEventCount[14] | Per-position `uint16` count of distinct math-saturation episodes since MCU boot |
| 992 | 28 | PressureMathSaturationAttemptCount[14] | Per-position `uint16` count of automatic I2C bus-recovery/re-measurement attempts since MCU boot |
| 1020 | 28 | PressureMathSaturationSuccessCount[14] | Per-position `uint16` count of episodes that returned to valid samples and completed fault-latch recovery since MCU boot |

Total payload length: `1048` bytes. The Qt decoder remains backward compatible with older `118`, `190`, `196`, `208`, `224`, `238`, `956`, and `964` byte snapshots. A shorter legacy snapshot means unavailable tail diagnostics are initialized to zero; calibration availability must not be presented as a confirmed saved profile when its fields are absent.

Math-saturation counters are RAM diagnostics and reset on MCU reboot; firmware must not write them to Flash on every event. A continuous `0x41` condition counts as one event. The MCU may perform at most three automatic bus-recovery/re-measurement attempts for that event, at the initial detection, after `500ms`, and after `1000ms`. Recovery success is counted only after valid samples return and the normal five-sample hot-plug latch-release condition completes. If `0x41` persists, pressure remains invalid and the safety latch remains active because the current PCB does not expose an independently controlled MPRLS `RES` or sensor power switch.

The Qt architecture view consumes this full snapshot directly: valve blinking comes from `ValveOpenMask`, tank and channel pressure labels come from `Pressure001mmHg` plus `PressureValidMask`, PCBA connection/result badges come from the PCBA masks and `PcbaPressure001mmHg`, the PCBA current debug page comes from the current arrays plus current valid masks, `PcbaCurrentFlags`, and `PcbaCurrentRawAdc`, the ADC realtime reference page comes from `Adc*` fields, and the RTC debug page comes from `RtcEpochSeconds` plus `RtcFlags`. `WorkflowFlags` bit4 is reserved for the dedicated `单PCBA全流程测试` path so the host can distinguish "only channel 1 is intentionally running" from the normal eight-channel production flow. If a pressure sensor or current valid bit is clear, the PC must display `--` instead of showing a fake value or treating `0` as a real reading.

`PcbaCurrentFlags` bit0 selects which current array the Qt PCBA current debug page should treat as the current display. When bit0 is clear, current is shown in uA from `PcbaStandbyCurrentUaX100` and `PcbaStandbyCurrentValidMask`. When bit0 is set, current is shown in mA from `PcbaWorkCurrentUaX100` and `PcbaWorkCurrentValidMask`. `PcbaPowerFlags` tells the same debug page whether the fixture is currently feeding PCBA `5V`, `4.5V`, both, or neither. The debug page displays only the currently selected range, not separate standby/work columns. The current value is corrected by the realtime internal-reference coefficient; `PcbaCurrentRawAdc` remains the uncorrected ADC raw code. The same page also shows the current internal-reference correction coefficient from `AdcScalePpm / 1000000`; this coefficient is refreshed in firmware before every multi-channel PCBA current capture and is not saved to Flash.

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
- Command dispatch for `HELLO`, `GET_STATUS`, `START`, `STOP`, `PAUSE`, `RESUME`, `SET_STATE`, `SET_THRESHOLD`, `MANUAL_VALVE`, `ENTER_MSC_REBOOT`, `SET_RTC_TIME`, `SET_PCBA_CURRENT_RANGE`, `CALIBRATE_ADC`, `SET_VALVE_MASK`, `SINGLE_TANK_LOOP`, `RUN_PCBA_TIMING`, `GET_PCBA_TIMING`, `RUN_SINGLE_TANK_PCBA`, `GET_SINGLE_TANK_PCBA`, `SET_PCBA_SUPPLY_VOLTAGE`, `SENSOR_CAL_ACTION`, and `GET_SENSOR_CAL_STATUS`.
- Periodic `STATUS_SNAPSHOT` reporting from existing valve, pressure, PCBA, and state-machine getters.

### Calibration Flash preservation

Pressure calibration uses two reserved internal-Flash pages:

- Page A: `0x0807F000` through `0x0807F7FF`.
- Page B: `0x0807F800` through `0x0807FFFF`.

Each image contains a schema version, monotonically increasing sequence, 14 profiles, calibrated mask, CRC32, and a commit marker written last. The MCU writes and verifies the inactive page before selecting it, so the previous valid page survives an interrupted update.

Firmware images and linker regions must end before `0x0807F000`. Firmware update scripts must use range/sector erase only and must never use full-chip or mass erase, because mass erase destroys both calibration pages. If a full-chip erase is explicitly required for recovery, all 14 pressure-sensor positions must be treated as uncalibrated and calibrated again before production use.

Still not included yet:

- Actual STM32 USB Device Core, CDC class, and MSC class files.
- Concrete implementations of `UsbCdcControl_Start`, `UsbCdcControl_Read`, and `UsbCdcControl_Write`.
- MSC boot persistence and reboot implementation behind `UsbCdcControl_RequestMscReboot`.
