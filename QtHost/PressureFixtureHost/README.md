# 气压检测工装 Qt 上位机

## 功能

- 在主界面中重绘气压检测工装架构图。
- 直接在架构图器件上显示并闪烁提示：
  - 阀门开闭状态。
  - 6 个气罐压力。
  - 14 路压力检测值。
  - 8 路 PCBA 在线状态。
  - 8 路 PCBA 测得气压值和结果。
- 支持 USB CDC 虚拟串口连接 MCU。
- 支持本地仿真模式：STM32 USB 未接入电脑时也能预览流程和界面。
- 支持上位机控制下位机：
  - 开始、停止、暂停、继续。
  - 切换指定流程状态。
  - 下发压力误差阈值。
  - 手动控制单个阀门。
  - 请求 MCU 重启进入 USB MSC U 盘维护模式。

## USB 方案

正常业务模式设计为 USB CDC ACM 虚拟串口，Qt 按二进制协议与 MCU 交互。

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

1. 先接入 STM32 USB Device Core，并实现固件里的 `UsbCdcControl_*` 端口函数。
2. 用 J-Link 下载 MCU 固件。
3. STM32 USB 接电脑后，在上位机右侧点击 `刷新`。
4. 选择新出现的 `COMx`，点击 `连接`。
5. 上位机会发送 `HELLO` 和 `GET_STATUS`。
6. MCU 周期发送 `STATUS_SNAPSHOT` 后，架构图会直接由真实状态刷新。

协议详见：

```text
..\..\Docs\PC-MCU_USB_Control_Protocol.md
```
