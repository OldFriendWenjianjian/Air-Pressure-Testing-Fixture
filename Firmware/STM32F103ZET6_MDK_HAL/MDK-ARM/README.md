# Keil MDK 工程创建说明

1. 新建 Keil MDK 工程，芯片选择 `STM32F103ZETx`。
2. 加入 `Core/Src`、`App/Src`、`BSP/Src`、`USB_DEVICE/App` 下的 `.c` 文件。
3. Include Path 加入：
   - `../Core/Inc`
   - `../App/Inc`
   - `../BSP/Inc`
   - `../USB_DEVICE/App`
   - STM32CubeF1 HAL/CMSIS/USB Device 头文件目录
4. 加入 STM32CubeF1 HAL 源文件，至少包括 RCC/GPIO/ADC/SPI/UART/CORTEX/FLASH/PCD。
5. 加入启动文件 `startup_stm32f103xe.s`。
6. 若启用 USB MSC，加入 ST USB Device Core、MSC Class，并让 `usbd_storage_if.c` 调用 `storage_w25q_msc.c` 中的接口。CubeMX/USB 中间件生成的 `MX_USB_DEVICE_Init()` 会被 `usb_msc_app.c` 调用。
