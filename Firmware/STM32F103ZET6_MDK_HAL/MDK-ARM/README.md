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
3. 长按 `KEY1(PC3)` 上电会进入 U 盘维护模式入口；当前工程已完成 W25Q128 块读写适配，但 USB Device Core/MSC Class 仍需用 STM32CubeF1 或 Keil Middleware 接入后才会在电脑端枚举成 U 盘。
4. 后续真实硬件 bring-up 时，可以保留业务层代码，用 STM32CubeF1 官方 HAL/USB 栈替换最小兼容层和 weak `MX_USB_DEVICE_Init()`。
