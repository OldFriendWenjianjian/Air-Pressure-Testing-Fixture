#include "app_pcba_uart.h"
#include "app_config.h"
#include "main.h"
#include "stm32f1xx_hal.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;

static UART_HandleTypeDef *const s_hw_uarts[5] = {
    &huart1, &huart2, &huart3, &huart4, &huart5
};

typedef struct {
    GPIO_TypeDef *tx_port;
    uint16_t tx_pin;
    GPIO_TypeDef *rx_port;
    uint16_t rx_pin;
} SoftUartPin;

static const SoftUartPin s_soft_uart_pins[3] = {
    {GPIOE, GPIO_PIN_0, GPIOE, GPIO_PIN_1},
    {GPIOE, GPIO_PIN_2, GPIOE, GPIO_PIN_3},
    {GPIOE, GPIO_PIN_4, GPIOE, GPIO_PIN_5}
};

static void dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static void soft_uart_write_bit(const SoftUartPin *pin, uint8_t value, uint32_t bit_cycles)
{
    HAL_GPIO_WritePin(pin->tx_port, pin->tx_pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    dwt_delay_cycles(bit_cycles);
}

static int send_soft_uart(uint8_t channel, const uint8_t *data, uint16_t len)
{
    if (channel < 6u || channel > 8u || data == 0) {
        return -1;
    }

    const SoftUartPin *pin = &s_soft_uart_pins[channel - 6u];
    uint32_t bit_cycles = HAL_RCC_GetHCLKFreq() / APP_PCBA_UART_BAUDRATE;

    __disable_irq();
    for (uint16_t i = 0; i < len; ++i) {
        uint8_t b = data[i];

        soft_uart_write_bit(pin, 0u, bit_cycles);
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            soft_uart_write_bit(pin, (uint8_t)((b >> bit) & 0x01u), bit_cycles);
        }
        soft_uart_write_bit(pin, 1u, bit_cycles);
    }
    __enable_irq();

    return 0;
}

static int recv_soft_uart(uint8_t channel, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (channel < 6u || channel > 8u || data == 0) {
        return -1;
    }

    const SoftUartPin *pin = &s_soft_uart_pins[channel - 6u];
    uint32_t bit_cycles = HAL_RCC_GetHCLKFreq() / APP_PCBA_UART_BAUDRATE;
    uint32_t start_tick = HAL_GetTick();

    for (uint16_t i = 0; i < len; ++i) {
        while (HAL_GPIO_ReadPin(pin->rx_port, pin->rx_pin) != GPIO_PIN_RESET) {
            if ((HAL_GetTick() - start_tick) > timeout_ms) {
                return -1;
            }
        }

        dwt_delay_cycles(bit_cycles + (bit_cycles / 2u));
        uint8_t b = 0u;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if (HAL_GPIO_ReadPin(pin->rx_port, pin->rx_pin) == GPIO_PIN_SET) {
                b |= (uint8_t)(1u << bit);
            }
            dwt_delay_cycles(bit_cycles);
        }
        dwt_delay_cycles(bit_cycles);
        data[i] = b;
    }

    return 0;
}

static int recv_bytes(uint8_t channel, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (channel <= 5u) {
        return HAL_UART_Receive(s_hw_uarts[channel - 1u], data, len, timeout_ms) == HAL_OK ? 0 : -1;
    }

    return recv_soft_uart(channel, data, len, timeout_ms);
}

static int receive_frame(uint8_t channel, PcbaFrame *response, uint32_t timeout_ms)
{
    uint8_t rx[PCBA_FRAME_MAX_SIZE];

    if (response == 0) {
        return -1;
    }
    if (recv_bytes(channel, rx, 6u, timeout_ms) != 0) {
        return -1;
    }
    if (rx[0] != PCBA_FRAME_HEAD0 || rx[1] != PCBA_FRAME_HEAD1) {
        return -1;
    }

    uint16_t data_len = (uint16_t)(rx[4] | ((uint16_t)rx[5] << 8));
    if (data_len > PCBA_FRAME_MAX_DATA) {
        return -1;
    }
    if (recv_bytes(channel, &rx[6], (uint16_t)(data_len + 2u), timeout_ms) != 0) {
        return -1;
    }

    return PcbaProtocol_Parse(rx, (size_t)(6u + data_len + 2u), response) ? 0 : -1;
}

void AppPcbaUart_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __HAL_RCC_GPIOE_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOE, &gpio);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_4, GPIO_PIN_SET);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOE, &gpio);
}

int AppPcbaUart_Send(uint8_t channel, const uint8_t *data, uint16_t len)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT || data == 0) {
        return -1;
    }

    if (channel <= 5u) {
        return HAL_UART_Transmit(s_hw_uarts[channel - 1u], (uint8_t *)data, len, 100u) == HAL_OK ? 0 : -1;
    }

    return send_soft_uart(channel, data, len);
}

int AppPcbaUart_SendCommandAll(uint8_t cmd)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildNoData(cmd, ch, frame, sizeof(frame));
        if (len == 0u || AppPcbaUart_Send(ch, frame, (uint16_t)len) != 0) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_SendPressureAll(uint8_t cmd, uint32_t pressure_001mmhg)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildPressure(cmd, ch, pressure_001mmhg, frame, sizeof(frame));
        if (len == 0u || AppPcbaUart_Send(ch, frame, (uint16_t)len) != 0) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_RequestAll(uint8_t cmd, PcbaFrame *responses, uint32_t timeout_ms)
{
    uint8_t tx[PCBA_FRAME_MAX_SIZE];
    PcbaFrame local_responses[APP_PCBA_CHANNEL_COUNT];

    if (responses == 0) {
        responses = local_responses;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildNoData(cmd, ch, tx, sizeof(tx));
        if (len == 0u || AppPcbaUart_Send(ch, tx, (uint16_t)len) != 0) {
            return -1;
        }
        if (receive_frame(ch, &responses[ch - 1u], timeout_ms) != 0) {
            return -1;
        }
    }

    return 0;
}
