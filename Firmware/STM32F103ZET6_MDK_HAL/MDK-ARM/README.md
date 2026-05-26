# Keil MDK 工程说明

工程文件：`PressureFixture_STM32F103ZET6.uvprojx`

当前工程使用 Keil MDK ARMCLANG，目标芯片为 `STM32F103ZE`。仓库内已包含最小 CMSIS/HAL 兼容层、启动文件和系统时钟文件，因此可以在未安装 STM32CubeF1 Pack 的机器上完成 MDK 编译。

本地批量编译命令示例：

```powershell
& 'C:\Users\a1258\AppData\Local\Keil_v5\UV4\UV4.exe' -b '.\PressureFixture_STM32F103ZET6.uvprojx' -o '.\uv4-build.log'
```

成功时日志末尾应显示：

```text
".\Objects\PressureFixture_STM32F103ZET6.axf" - 0 Error(s), 0 Warning(s).
```

说明：

1. `Drivers/STM32F1xx_HAL_Driver/Src/min_hal.c` 是为了让当前业务软件、协议状态机和 BSP 在 MDK 下可编译的最小兼容层。
2. 后续进入真实硬件 bring-up 时，建议用 STM32CubeF1 官方 HAL 替换该最小兼容层，并补齐 USB Device Core/MSC Class。
3. 目前 `usb_msc_app.c` 中的 `MX_USB_DEVICE_Init()` 是 weak stub；接入 CubeMX USB 设备库后，同名强符号会自动覆盖它。
