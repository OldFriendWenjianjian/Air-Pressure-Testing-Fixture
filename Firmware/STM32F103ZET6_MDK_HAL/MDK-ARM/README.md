# Keil MDK 工程说明

工程文件：`PressureFixture_STM32F103ZET6.uvprojx`

当前工程使用 Keil MDK ARMCLANG，目标芯片为 `STM32F103ZE`。仓库内已包含最小 CMSIS/HAL 兼容层、启动文件和系统时钟文件，因此可以在未安装 STM32CubeF1 Pack 的机器上完成 MDK 编译并生成 `AXF/HEX`。

本地批量编译命令示例：

```powershell
& 'C:\Users\a1258\AppData\Local\Keil_v5\UV4\UV4.exe' -b '.\PressureFixture_STM32F103ZET6.uvprojx' -o '.\uv4-build.log'
```

成功时日志末尾应显示：

```text
".\Objects\PressureFixture_STM32F103ZET6.axf" - 0 Error(s), 0 Warning(s).
```

MDK 下载步骤：

1. 打开 `PressureFixture_STM32F103ZET6.uvprojx`。
2. 点击 `Project -> Rebuild all target files`，确认 0 Error(s), 0 Warning(s)。
3. 连接下载器后点击 `Flash -> Download`，或工具栏 `LOAD`。
4. 工程默认写入了 Cortex-M3/ULINK2 下载配置和 `STM32F10x_512` 内部 Flash 算法。若现场使用 J-Link 或 ST-Link，请在 `Options for Target -> Debug` 切换对应 Adapter，`Utilities -> Settings` 保持 Flash 起始地址 `0x08000000`、大小 `0x00080000`。

说明：

1. `Drivers/STM32F1xx_HAL_Driver/Src/min_hal.c` 是当前工程自带的最小寄存器级 HAL 兼容层，已实现本项目用到的 GPIO、SysTick、RCC、ADC1、SPI2/SPI3、USART1/2/3、UART4/5 轮询收发。
2. PCBA 串口固定为 `115200 8N1`，由 `App/Inc/app_config.h` 的 `APP_PCBA_UART_BAUDRATE` 定义。
3. 正常业务模式已接入最小 USB CDC ACM 设备，枚举后 Windows 会出现新的 `COMx`，Qt 上位机按现有二进制协议连接该串口控制 MCU。
4. 长按 `KEY1(PC3)` 上电会进入 USB MSC 维护模式，W25Q128 以单 LUN、512 字节块的 U 盘方式暴露给电脑。
5. 当前版本采用“正常 CDC / 长按 KEY1 MSC 维护入口”的双模式。若后续需要 CDC 与 MSC 同时在线，再引入 STM32CubeF1 官方 USB Device Core 的 CDC+MSC 复合设备。
