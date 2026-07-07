#include "main.h"
#include "app_config.h"
#include "app_adc_calibration.h"
#include "app_display.h"
#include "app_jlink_rtt_control.h"
#include "app_keys.h"
#include "app_power.h"
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
static void MX_SPI2_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_UART4_Init(void);
static void MX_UART5_Init(void);
static AppBootMode DetectBootMode(void);
#if APP_VALVE1_0_1HZ_TEST_ENABLED
static void Valve1BlinkTest_Task(void);
#endif
#if APP_LT768_STRESS_TEST_ENABLED
static void Lt768StressTest_Task(void);
#endif

/*
 * Local LCD UI must stay enabled in normal boot even when J-Link RTT is active.
 * The screen uses SPI2/LT768 while the PC link uses RTT + SPI3/USARTx, so
 * disabling the display here only leaves LCD_RST held low and the panel black.
 */
#define APP_ENABLE_LOCAL_DISPLAY 1

static void EnsureLocalDisplayReady(AppBootMode boot_mode)
{
    static uint32_t s_last_retry_ms = 0u;

    if (!APP_ENABLE_LOCAL_DISPLAY || boot_mode != APP_MODE_NORMAL) {
        return;
    }
    if (AppDisplay_NeedsHardwareInit() == 0u) {
        return;
    }
    if ((HAL_GetTick() - s_last_retry_ms) < 1500u) {
        return;
    }
    s_last_retry_ms = HAL_GetTick();

    HAL_Delay(120u);
    if (hspi2.Instance == 0) {
        MX_SPI2_Init();
    }
    LT768_BasicInit();
    AppDisplay_NotifyHardwareReady();
}

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

#if APP_LT768_COLOR_BAR_TEST_ENABLED || APP_LT768_FRAMEBUFFER_TEST_ENABLED || APP_LT768_STRESS_TEST_ENABLED
    AppPower_Enable5V();
    HAL_Delay(120u);
    g_lt768_progress[0] = 0xA100u;
    MX_SPI2_Init();
    g_lt768_progress[0] = 0xA101u;
    LT768_BasicInit();
    g_lt768_progress[0] = 0xA102u;
#if APP_LT768_COLOR_BAR_TEST_ENABLED
    LT768_EnableColorBarTest();
#elif APP_LT768_FRAMEBUFFER_TEST_ENABLED
    LT768_ShowFramebufferTest();
#else
    Lt768StressTest_Task();
#endif
    g_lt768_progress[0] = 0xA103u;
    while (1) {
        g_lt768_progress[7]++;
#if APP_LT768_COLOR_BAR_TEST_ENABLED
        LT768_CycleDisplayCompatibilityMode();
#endif
      HAL_Delay(300u);
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

    MX_SPI3_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_SPI3\n");
#endif
    MX_USART1_UART_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_UART1\n");
#endif
    MX_USART2_UART_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_UART2\n");
#endif
    MX_USART3_UART_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_UART3\n");
#endif
    MX_UART4_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_UART4\n");
#endif
    MX_UART5_Init();
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_UART5\n");
#endif

    AppStateMachine_Init(boot_mode);
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_STATE_INIT\n");
#endif
    AppUsbControl_Init(boot_mode);
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("AFTER_USB_CTRL_INIT\n");
#endif
    if (APP_ENABLE_LOCAL_DISPLAY) {
        if (boot_mode == APP_MODE_NORMAL) {
            /*
             * Keep LT768 bring-up after AppStateMachine_Init(): the state
             * machine init drives AppPower_AllOff(), which shares the LCD 5V rail.
             * Also allow retry later from the main loop so "USB first, 24V later"
             * can still light the screen without a manual reset.
             */
            EnsureLocalDisplayReady(boot_mode);
        }
        AppDisplay_Init(boot_mode);
        EnsureLocalDisplayReady(boot_mode);
    }

    while (1) {
        AppStateMachine_Task();
        AppUsbControl_Task();
        if (APP_ENABLE_LOCAL_DISPLAY) {
            EnsureLocalDisplayReady(boot_mode);
            AppDisplay_Task();
        }
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

#if APP_LT768_STRESS_TEST_ENABLED
static void stress_fill_vertical_stripes(uint16_t stripe_w, uint32_t color_a, uint32_t color_b)
{
    for (uint16_t x = 0u; x < LT768_SCREEN_WIDTH; x = (uint16_t)(x + stripe_w)) {
        const uint16_t x2 = (uint16_t)((x + stripe_w - 1u) >= LT768_SCREEN_WIDTH ?
                                      (LT768_SCREEN_WIDTH - 1u) :
                                      (x + stripe_w - 1u));
        const uint32_t color = (((x / stripe_w) & 1u) == 0u) ? color_a : color_b;
        LT768_FillRect(x, 0u, x2, (uint16_t)(LT768_SCREEN_HEIGHT - 1u), color);
    }
}

static void stress_fill_horizontal_stripes(uint16_t stripe_h, uint32_t color_a, uint32_t color_b)
{
    for (uint16_t y = 0u; y < LT768_SCREEN_HEIGHT; y = (uint16_t)(y + stripe_h)) {
        const uint16_t y2 = (uint16_t)((y + stripe_h - 1u) >= LT768_SCREEN_HEIGHT ?
                                      (LT768_SCREEN_HEIGHT - 1u) :
                                      (y + stripe_h - 1u));
        const uint32_t color = (((y / stripe_h) & 1u) == 0u) ? color_a : color_b;
        LT768_FillRect(0u, y, (uint16_t)(LT768_SCREEN_WIDTH - 1u), y2, color);
    }
}

static void stress_fill_color_bars(uint16_t offset)
{
    static const uint32_t colors[] = {
        LT768_COLOR_WHITE,
        LT768_COLOR_YELLOW,
        LT768_COLOR_GREEN,
        LT768_COLOR_BLUE,
        LT768_COLOR_RED,
        LT768_COLOR_LIGHT_GRAY,
        LT768_COLOR_DARK_GRAY,
        LT768_COLOR_BLACK
    };
    const uint16_t color_count = (uint16_t)(sizeof(colors) / sizeof(colors[0]));
    const uint16_t bar_w = (uint16_t)(LT768_SCREEN_WIDTH / color_count);

    for (uint16_t i = 0u; i < color_count; ++i) {
        const uint16_t slot = (uint16_t)((i + offset) % color_count);
        const uint16_t x1 = (uint16_t)(i * bar_w);
        const uint16_t x2 = (i == (color_count - 1u)) ?
                            (uint16_t)(LT768_SCREEN_WIDTH - 1u) :
                            (uint16_t)(x1 + bar_w - 1u);
        LT768_FillRect(x1, 0u, x2, (uint16_t)(LT768_SCREEN_HEIGHT - 1u), colors[slot]);
    }
}

static void stress_fill_checkerboard(uint16_t block, uint8_t invert)
{
    for (uint16_t y = 0u; y < LT768_SCREEN_HEIGHT; y = (uint16_t)(y + block)) {
        const uint16_t y2 = (uint16_t)((y + block - 1u) >= LT768_SCREEN_HEIGHT ?
                                      (LT768_SCREEN_HEIGHT - 1u) :
                                      (y + block - 1u));
        for (uint16_t x = 0u; x < LT768_SCREEN_WIDTH; x = (uint16_t)(x + block)) {
            const uint16_t x2 = (uint16_t)((x + block - 1u) >= LT768_SCREEN_WIDTH ?
                                          (LT768_SCREEN_WIDTH - 1u) :
                                          (x + block - 1u));
            const uint8_t odd = (uint8_t)((((x / block) + (y / block)) & 1u) ^ invert);
            LT768_FillRect(x, y, x2, y2, odd ? LT768_COLOR_WHITE : LT768_COLOR_BLACK);
        }
    }
}

static void stress_moving_window(uint16_t phase)
{
    static const uint32_t bg[] = {
        LT768_COLOR_BLACK,
        LT768_COLOR_WHITE,
        LT768_COLOR_BLUE,
        LT768_COLOR_RED
    };
    const uint16_t x = (uint16_t)((phase * 37u) % (LT768_SCREEN_WIDTH - 220u));
    const uint16_t y = (uint16_t)((phase * 23u) % (LT768_SCREEN_HEIGHT - 140u));

    LT768_Clear(bg[phase & 3u]);
    LT768_FillRect(x, y, (uint16_t)(x + 219u), (uint16_t)(y + 139u), LT768_COLOR_YELLOW);
    LT768_FillRect((uint16_t)(x + 24u),
                   (uint16_t)(y + 20u),
                   (uint16_t)(x + 195u),
                   (uint16_t)(y + 119u),
                   LT768_COLOR_GREEN);
    LT768_FillRect((uint16_t)(x + 62u),
                   (uint16_t)(y + 44u),
                   (uint16_t)(x + 157u),
                   (uint16_t)(y + 95u),
                   LT768_COLOR_RED);
}

static void Lt768StressTest_Task(void)
{
    uint16_t phase = 0u;

    LT768_Clear(LT768_COLOR_BLACK);
    LT768_DrawText(24u, 20u, LT768_COLOR_WHITE, LT768_COLOR_BLACK, "LT768 LCD CABLE STRESS TEST");
    LT768_DrawText(24u, 44u, LT768_COLOR_YELLOW, LT768_COLOR_BLACK, "Watch for flicker, wrong color, noise lines");
    HAL_Delay(1200u);

    while (1) {
        LT768_Clear(LT768_COLOR_WHITE);
        HAL_Delay(120u);
        LT768_Clear(LT768_COLOR_BLACK);
        HAL_Delay(120u);
        LT768_Clear(LT768_COLOR_RED);
        HAL_Delay(120u);
        LT768_Clear(LT768_COLOR_GREEN);
        HAL_Delay(120u);
        LT768_Clear(LT768_COLOR_BLUE);
        HAL_Delay(120u);

        stress_fill_vertical_stripes(1u, LT768_COLOR_WHITE, LT768_COLOR_BLACK);
        HAL_Delay(260u);
        stress_fill_vertical_stripes(1u, LT768_COLOR_BLACK, LT768_COLOR_WHITE);
        HAL_Delay(260u);
        stress_fill_horizontal_stripes(1u, LT768_COLOR_WHITE, LT768_COLOR_BLACK);
        HAL_Delay(260u);
        stress_fill_horizontal_stripes(1u, LT768_COLOR_BLACK, LT768_COLOR_WHITE);
        HAL_Delay(260u);

        stress_fill_vertical_stripes(4u, LT768_COLOR_RED, LT768_COLOR_BLUE);
        HAL_Delay(260u);
        stress_fill_horizontal_stripes(4u, LT768_COLOR_GREEN, LT768_COLOR_YELLOW);
        HAL_Delay(260u);
        stress_fill_checkerboard(8u, (uint8_t)(phase & 1u));
        HAL_Delay(260u);
        stress_fill_color_bars((uint16_t)(phase & 7u));
        HAL_Delay(260u);
        stress_moving_window(phase);
        HAL_Delay(120u);

        phase = (uint16_t)(phase + 1u);
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

static AppBootMode DetectBootMode(void)
{
#if APP_PC_LINK_JLINK_RTT_ENABLED
#if APP_PC_LINK_JLINK_RTT_ENABLED
        AppJlinkRttControl_DebugText("BOOT_CHECK_KEY1\n");
#endif
#endif
    if (AppKeys_Key1HeldAtBoot(APP_KEY1_HOLD_TO_MSC_MS)) {
#if APP_PC_LINK_JLINK_RTT_ENABLED
#if APP_PC_LINK_JLINK_RTT_ENABLED
            AppJlinkRttControl_DebugText("BOOT_MODE=USB_MSC\n");
#endif
#endif
        return APP_MODE_USB_MSC;
    }

#if APP_PC_LINK_JLINK_RTT_ENABLED
#if APP_PC_LINK_JLINK_RTT_ENABLED
    AppJlinkRttControl_DebugText("BOOT_MODE=NORMAL\n");
#endif
#endif
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
    /*
     * LT768 reference code drives the panel over a slow software SPI.
     * Keep SPI2 conservative here to avoid marginal timing on the
     * LCD module during reset/config writes.
     */
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

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
