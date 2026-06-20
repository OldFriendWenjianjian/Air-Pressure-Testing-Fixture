#include "app_jlink_uart_control.h"

#include "app_config.h"
#include "app_usb_control.h"
#include "main.h"

#if APP_PC_LINK_JLINK_UART8_ENABLED

#define PC_LINK_TX_PORT            GPIOE
#define PC_LINK_TX_PIN             GPIO_PIN_4
#define PC_LINK_RX_PORT            GPIOE
#define PC_LINK_RX_PIN             GPIO_PIN_5
#define PC_LINK_EXTI_LINE          (1UL << 5)
#define PC_LINK_RX_RING_SIZE       256u

static volatile uint8_t s_rx_ring[PC_LINK_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint8_t s_msc_reboot_pending;
static uint32_t s_bit_cycles;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static void rx_push(uint8_t value)
{
    uint16_t next = (uint16_t)((s_rx_head + 1u) % PC_LINK_RX_RING_SIZE);

    if (next == s_rx_tail) {
        return;
    }
    s_rx_ring[s_rx_head] = value;
    s_rx_head = next;
}

static void tx_write_byte(uint8_t value)
{
    HAL_GPIO_WritePin(PC_LINK_TX_PORT, PC_LINK_TX_PIN, GPIO_PIN_RESET);
    dwt_delay_cycles(s_bit_cycles);
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        HAL_GPIO_WritePin(PC_LINK_TX_PORT,
                          PC_LINK_TX_PIN,
                          ((value >> bit) & 0x01u) != 0u ? GPIO_PIN_SET : GPIO_PIN_RESET);
        dwt_delay_cycles(s_bit_cycles);
    }
    HAL_GPIO_WritePin(PC_LINK_TX_PORT, PC_LINK_TX_PIN, GPIO_PIN_SET);
    dwt_delay_cycles(s_bit_cycles);
}

static void configure_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = PC_LINK_TX_PIN;
    HAL_GPIO_WritePin(PC_LINK_TX_PORT, PC_LINK_TX_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(PC_LINK_TX_PORT, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = PC_LINK_RX_PIN;
    HAL_GPIO_Init(PC_LINK_RX_PORT, &gpio);
}

static void configure_exti(void)
{
    AFIO->EXTICR[1] = (AFIO->EXTICR[1] & ~(0x0FUL << 4)) | (0x04UL << 4);
    EXTI->IMR &= ~PC_LINK_EXTI_LINE;
    EXTI->EMR &= ~PC_LINK_EXTI_LINE;
    EXTI->RTSR &= ~PC_LINK_EXTI_LINE;
    EXTI->FTSR |= PC_LINK_EXTI_LINE;
    EXTI->PR = PC_LINK_EXTI_LINE;
    EXTI->IMR |= PC_LINK_EXTI_LINE;

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0u, 0u);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

int UsbCdcControl_Start(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_msc_reboot_pending = 0u;
    s_bit_cycles = HAL_RCC_GetHCLKFreq() / APP_PC_LINK_UART_BAUDRATE;
    if (s_bit_cycles == 0u) {
        s_bit_cycles = 1u;
    }

    dwt_init();
    configure_gpio();
    configure_exti();
    return 0;
}

int UsbCdcControl_Read(uint8_t *data, uint16_t max_len)
{
    uint16_t count = 0u;

    if (data == 0 || max_len == 0u) {
        return 0;
    }

    __disable_irq();
    while (count < max_len && s_rx_tail != s_rx_head) {
        data[count++] = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % PC_LINK_RX_RING_SIZE);
    }
    __enable_irq();

    return (int)count;
}

int UsbCdcControl_Write(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u) {
        return 0;
    }

    __disable_irq();
    for (uint16_t i = 0u; i < len; ++i) {
        tx_write_byte(data[i]);
    }
    __enable_irq();

    return (int)len;
}

void UsbCdcControl_RequestMscReboot(void)
{
    s_msc_reboot_pending = 1u;
}

uint8_t UsbCdcControl_IsMscRebootPending(void)
{
    return s_msc_reboot_pending;
}

void AppJlinkUartControl_ExtiIrqHandler(void)
{
    uint8_t value = 0u;

    if ((EXTI->PR & PC_LINK_EXTI_LINE) == 0u) {
        return;
    }

    EXTI->IMR &= ~PC_LINK_EXTI_LINE;
    EXTI->PR = PC_LINK_EXTI_LINE;

    dwt_delay_cycles(s_bit_cycles / 2u);
    if (HAL_GPIO_ReadPin(PC_LINK_RX_PORT, PC_LINK_RX_PIN) != GPIO_PIN_RESET) {
        EXTI->IMR |= PC_LINK_EXTI_LINE;
        return;
    }

    dwt_delay_cycles(s_bit_cycles);
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        if (HAL_GPIO_ReadPin(PC_LINK_RX_PORT, PC_LINK_RX_PIN) == GPIO_PIN_SET) {
            value |= (uint8_t)(1u << bit);
        }
        dwt_delay_cycles(s_bit_cycles);
    }

    rx_push(value);
    EXTI->IMR |= PC_LINK_EXTI_LINE;
}

#else

void AppJlinkUartControl_ExtiIrqHandler(void)
{
}

#endif
