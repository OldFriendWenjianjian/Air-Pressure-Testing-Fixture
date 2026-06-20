#include "main.h"
#include "app_config.h"
#include "app_adc_calibration.h"
#include "app_display.h"
#include "app_keys.h"
#include "app_rtc.h"
#include "app_state_machine.h"
#include "app_usb_control.h"
#include "app_valves.h"
#include "board_pins.h"
#include "lt768_basic.h"

ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart5;

static void SystemClock_Config(void);
static void MX_ADC1_Init(void);
#if !APP_PC_LINK_BRINGUP_ONLY_ENABLED
static void MX_SPI2_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_UART4_Init(void);
static void MX_UART5_Init(void);
static AppBootMode DetectBootMode(void);
#endif
#if APP_VALVE1_0_1HZ_TEST_ENABLED
static void Valve1BlinkTest_Task(void);
#endif

int main(void)
{
    HAL_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    (void)UsbCdcControl_Start();
#endif

    SystemClock_Config();

    BoardPins_EnableAllGpioClocks();
    BoardPins_ConfigOutputsSafe();
    BoardPins_ConfigKeys();

#if APP_LT768_COLOR_BAR_TEST_ENABLED || APP_LT768_FRAMEBUFFER_TEST_ENABLED
    MX_SPI2_Init();
    LT768_BasicInit();
#if APP_LT768_COLOR_BAR_TEST_ENABLED
    LT768_EnableColorBarTest();
#else
    LT768_ShowFramebufferTest();
#endif
    while (1) {
        HAL_Delay(100u);
    }
#endif

#if APP_VALVE1_0_1HZ_TEST_ENABLED
    Valve1BlinkTest_Task();
#endif

#if APP_PC_LINK_BRINGUP_ONLY_ENABLED
    MX_ADC1_Init();
    AppAdcCalibration_Init();
    AppRtc_Init();

    AppBootMode boot_mode = APP_MODE_NORMAL;
    AppStateMachine_Init(boot_mode);
    AppUsbControl_Init(boot_mode);

    while (1) {
        AppStateMachine_Task();
        AppUsbControl_Task();
        HAL_Delay(5u);
    }
#else
    MX_ADC1_Init();
    AppAdcCalibration_Init();
    AppRtc_Init();

    AppBootMode boot_mode = DetectBootMode();

    if (boot_mode == APP_MODE_NORMAL) {
        MX_SPI2_Init();
        LT768_BasicInit();
        LT768_ShowBootText("Pressure Fixture");
    }

    MX_SPI3_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_UART4_Init();
    MX_UART5_Init();

    AppStateMachine_Init(boot_mode);
    AppUsbControl_Init(boot_mode);
    AppDisplay_Init(boot_mode);

    while (1) {
        AppStateMachine_Task();
        AppUsbControl_Task();
        AppDisplay_Task();
        HAL_Delay(5u);
    }
#endif
}

#if APP_VALVE1_0_1HZ_TEST_ENABLED
static void Valve1BlinkTest_Task(void)
{
    uint8_t open = 0u;

    AppValves_AllClosed();
    while (1) {
        open = open == 0u ? 1u : 0u;
        AppValves_Set(1u, open);
        HAL_Delay(APP_VALVE1_TEST_HALF_PERIOD_MS);
    }
}
#endif

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }
    (void)HAL_ADCEx_Calibration_Start(&hadc1);
}

#if !APP_PC_LINK_BRINGUP_ONLY_ENABLED
static AppBootMode DetectBootMode(void)
{
    if (AppKeys_Key1HeldAtBoot(APP_KEY1_HOLD_TO_MSC_MS)) {
        return APP_MODE_USB_MSC;
    }

    return APP_MODE_NORMAL;
}

static void MX_SPI2_Init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_SPI3_Init(void)
{
    hspi3.Instance = SPI3;
    hspi3.Init.Mode = SPI_MODE_MASTER;
    hspi3.Init.Direction = SPI_DIRECTION_2LINES;
    hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi3.Init.NSS = SPI_NSS_SOFT;
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi3.Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(&hspi3) != HAL_OK) {
        Error_Handler();
    }
}

static void uart_common(UART_HandleTypeDef *huart, USART_TypeDef *instance)
{
    huart->Instance = instance;
    huart->Init.BaudRate = APP_PCBA_UART_BAUDRATE;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(huart) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    uart_common(&huart1, USART1);
}

static void MX_USART2_UART_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_AFIO_REMAP_USART2_ENABLE();
    uart_common(&huart2, USART2);
}

static void MX_USART3_UART_Init(void)
{
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_AFIO_REMAP_USART3_ENABLE();
    uart_common(&huart3, USART3);
}

static void MX_UART4_Init(void)
{
    __HAL_RCC_UART4_CLK_ENABLE();
    uart_common(&huart4, UART4);
}

static void MX_UART5_Init(void)
{
    __HAL_RCC_UART5_CLK_ENABLE();
    uart_common(&huart5, UART5);
}
#endif

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
