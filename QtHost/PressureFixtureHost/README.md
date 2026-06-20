# 气压检测工装 Qt J-Link RTT 上位机

## 功能

- 在主界面中重绘气压检测工装架构图。
- 直接在架构图器件上显示并闪烁提示：
  - 阀门开闭状态。
  - 6 个气罐压力。
  - 14 路压力检测值。
  - 8 路 PCBA 在线状态。
  - 8 路 PCBA 测得气压值和结果。
- 支持通过 SEGGER RTT 连接 MCU；当前联调只占用 J-Link SWD，不占用 UART8/PE4/PE5。
- 仍保留串口连接作为备用，正式模式可切回 STM32 USB CDC 虚拟串口。
- 未连接 MCU 时保持空状态，压力、阀门、PCBA 状态只显示真实快照。
- 支持上位机控制下位机：
  - 开始、停止、暂停、继续。
  - 切换指定流程状态。
  - 下发压力误差阈值。
  - 手动控制单个阀门。
  - 请求 MCU 重启进入 USB MSC U 盘维护模式。
  - 通过本机 J-Link 将 HEX 固件下载到 STM32F103ZE。

## 连接方案

正常业务模式设计为 USB CDC ACM 虚拟串口，Qt 按二进制协议与 MCU 交互。
当前硬件联调使用 SEGGER RTT，Qt 通过 `JLinkARM.dll` 的 RTT API 直接走 J-Link SWD 调试链路。
因此不需要连接 `PE4/PE5` 到 J-Link VCOM，UART8 可以保留给第 8 路 PCBA 通信。

U 盘维护功能继续保留：

- 长按 `KEY1` 上电进入 USB MSC U 盘模式。
- Qt 也提供 `重启到U盘` 命令，要求 MCU 安全关闭输出后重启到 USB MSC 模式。

第一版采用“正常 CDC / 维护 MSC”双模式，避免 STM32F103 USB 复合设备栈复杂度影响联调进度。

## 构建

推荐直接运行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build_qmake.ps1
```

生成程序：

```text
build-qmake\release\PressureFixtureHost.exe
```

也保留了 `CMakeLists.txt`，但当前 Windows 中文路径下 CMake 异常退出，qmake 构建已验证通过。

## 联调步骤

1. 确认 J-Link SWD 已连接 MCU 并上电。
2. 在左侧切到 `调试模式`，打开 `固件烧录`。
3. 默认 HEX 为 `Firmware\STM32F103ZET6_MDK_HAL\MDK-ARM\Objects\PressureFixture_STM32F103ZET6.hex`，也可以点 `选择HEX` 更换。
4. 点击 `下载到板子`，上位机会调用 `JLink.exe`，以 `STM32F103ZE / SWD / 100kHz` 执行擦除、下载、复位和运行；失败原因看面板里的 J-Link 日志。
5. 当前联调选择 `SEGGER RTT - J-Link SWD`。
6. 后续 STM32 USB CDC 接电脑后，也可以选择新出现的 `COMx` 作为备用串口连接。
7. 上位机会发送 `HELLO` 和 `GET_STATUS`。
8. MCU 周期发送 `STATUS_SNAPSHOT` 后，架构图会直接由真实状态刷新。

协议详见：

```text
..\..\Docs\PC-MCU_USB_Control_Protocol.md
```
