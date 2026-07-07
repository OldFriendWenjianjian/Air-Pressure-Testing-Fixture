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

#define APP_USART_SR_RXNE_BIT  (1u << 5)
#define APP_USART_CR1_RE_BIT   (1u << 2)
#define APP_USART_CR1_TE_BIT   (1u << 3)
#define APP_USART_CR1_UE_BIT   (1u << 13)

static void recover_hw_uart(UART_HandleTypeDef *huart)
{
    if (huart == 0 || huart->Instance == 0) {
        return;
    }

    while ((huart->Instance->SR & APP_USART_SR_RXNE_BIT) != 0u) {
        (void)huart->Instance->DR;
    }

    huart->Instance->CR1 &= (uint32_t)~APP_USART_CR1_UE_BIT;
    huart->Instance->CR1 |= APP_USART_CR1_TE_BIT | APP_USART_CR1_RE_BIT | APP_USART_CR1_UE_BIT;
}

static int send_hw_uart(uint8_t channel, const uint8_t *data, uint16_t len)
{
    UART_HandleTypeDef *huart = s_hw_uarts[channel - 1u];

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        recover_hw_uart(huart);
        if (HAL_UART_Transmit(huart, (uint8_t *)data, len, 250u) == HAL_OK) {
            return 0;
        }
        HAL_Delay(2u);
    }

    return -1;
}

static uint32_t dwt_now_us(void)
{
    uint32_t ms_a;
    uint32_t ms_b;
    uint32_t load;
    uint32_t val;
    uint32_t elapsed_in_ms_us;

    load = SysTick->LOAD + 1u;
    if (load == 0u) {
        return HAL_GetTick() * 1000u;
    }

    do {
        ms_a = HAL_GetTick();
        val = SysTick->VAL;
        ms_b = HAL_GetTick();
    } while (ms_a != ms_b);

    if (val >= load) {
        val = load - 1u;
    }

    elapsed_in_ms_us = ((load - val) * 1000u) / load;
    return (ms_a * 1000u) + elapsed_in_ms_us;
}

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

static void clear_pcba_frame(PcbaFrame *frame)
{
    if (frame == 0) {
        return;
    }
    frame->cmd = 0u;
    frame->channel = 0u;
    frame->len = 0u;
    frame->raw_len = 0u;
    frame->crc_ok = 0u;
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_DATA; ++i) {
        frame->data[i] = 0u;
    }
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_SIZE; ++i) {
        frame->raw[i] = 0u;
    }
}

static int receive_frame(uint8_t channel, PcbaFrame *response, uint32_t timeout_ms)
{
    uint8_t rx[PCBA_FRAME_MAX_SIZE];

    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    if (recv_bytes(channel, rx, 6u, timeout_ms) != 0) {
        return -1;
    }
    response->raw_len = 6u;
    for (uint8_t i = 0u; i < 6u; ++i) {
        response->raw[i] = rx[i];
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

    const uint8_t frame_len = (uint8_t)(6u + data_len + 2u);
    response->cmd = rx[2];
    response->channel = rx[3];
    response->len = data_len;
    response->raw_len = frame_len;
    for (uint8_t i = 0u; i < frame_len; ++i) {
        response->raw[i] = rx[i];
    }
    for (uint16_t i = 0u; i < data_len; ++i) {
        response->data[i] = rx[6u + i];
    }

    uint16_t rx_crc = (uint16_t)(rx[6u + data_len] | ((uint16_t)rx[7u + data_len] << 8));
    uint16_t calc_crc = PcbaProtocol_Crc16Modbus(&rx[2], (size_t)(1u + 1u + 2u + data_len));
    response->crc_ok = rx_crc == calc_crc ? 1u : 0u;
    return 0;
}

static void append_response_raw(PcbaFrame *response, const uint8_t *raw, uint8_t raw_len)
{
    if (response == 0 || raw == 0) {
        return;
    }

    response->raw_len = raw_len <= PCBA_FRAME_MAX_SIZE ? raw_len : PCBA_FRAME_MAX_SIZE;
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_SIZE; ++i) {
        response->raw[i] = i < response->raw_len ? raw[i] : 0u;
    }
    response->cmd = response->raw_len > 0u ? response->raw[0] : 0u;
    response->channel = 0u;
    response->len = response->raw_len;
    for (uint8_t i = 0u; i < PCBA_FRAME_MAX_DATA; ++i) {
        response->data[i] = i < response->raw_len ? response->raw[i] : 0u;
    }
    response->crc_ok = 0u;
}

static int receive_any_response(uint8_t channel, PcbaFrame *response, uint32_t timeout_ms)
{
    uint8_t raw[PCBA_FRAME_MAX_SIZE];
    uint8_t raw_len = 0u;
    uint8_t byte = 0u;

    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);

    if (recv_bytes(channel, &byte, 1u, timeout_ms) != 0) {
        return -1;
    }
    raw[raw_len++] = byte;

    if (byte == PCBA_FRAME_HEAD0) {
        if (recv_bytes(channel, &byte, 1u, timeout_ms) == 0) {
            raw[raw_len++] = byte;
            if (byte == PCBA_FRAME_HEAD1) {
                uint8_t head_tail[4];
                if (recv_bytes(channel, head_tail, sizeof(head_tail), timeout_ms) == 0) {
                    for (uint8_t i = 0u; i < sizeof(head_tail); ++i) {
                        raw[raw_len++] = head_tail[i];
                    }
                    uint16_t data_len = (uint16_t)(head_tail[2] | ((uint16_t)head_tail[3] << 8));
                    if (data_len <= PCBA_FRAME_MAX_DATA &&
                        raw_len + data_len + 2u <= PCBA_FRAME_MAX_SIZE &&
                        recv_bytes(channel, &raw[raw_len], (uint16_t)(data_len + 2u), timeout_ms) == 0) {
                        raw_len = (uint8_t)(raw_len + data_len + 2u);
                        (void)PcbaProtocol_Parse(raw, raw_len, response);
                        if (response->raw_len == 0u) {
                            append_response_raw(response, raw, raw_len);
                        }
                        return 0;
                    }
                }
            }
        }
        append_response_raw(response, raw, raw_len);
        return 0;
    }

    while (raw_len < PCBA_FRAME_MAX_SIZE && recv_bytes(channel, &byte, 1u, 5u) == 0) {
        raw[raw_len++] = byte;
    }
    append_response_raw(response, raw, raw_len);
    return 0;
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
        return send_hw_uart(channel, data, len);
    }

    return send_soft_uart(channel, data, len);
}

void AppPcbaUart_FlushRx(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return;
    }

    if (channel <= 5u) {
        UART_HandleTypeDef *huart = s_hw_uarts[channel - 1u];
        if (huart == 0 || huart->Instance == 0) {
            return;
        }
        while ((huart->Instance->SR & APP_USART_SR_RXNE_BIT) != 0u) {
            (void)huart->Instance->DR;
        }
        return;
    }

    {
        uint8_t byte;
        while (recv_bytes(channel, &byte, 1u, 0u) == 0) {
        }
    }
}

int AppPcbaUart_WakeOne(uint8_t channel, uint8_t expected, uint32_t timeout_ms)
{
    uint8_t wake = PCBA_WAKE_BYTE;
    uint8_t response = 0u;

    if (AppPcbaUart_Send(channel, &wake, 1u) != 0) {
        return -1;
    }
    if (recv_bytes(channel, &response, 1u, timeout_ms) != 0) {
        return -1;
    }
    return response == expected ? 0 : -1;
}

int AppPcbaUart_WakeOneTimed(uint8_t channel,
                             uint8_t expected,
                             uint32_t timeout_ms,
                             uint32_t *elapsed_us,
                             uint8_t *response_byte)
{
    uint8_t wake = PCBA_WAKE_BYTE;
    uint8_t response = 0u;
    uint32_t started_us = dwt_now_us();

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response_byte != 0) {
        *response_byte = 0u;
    }
    if (AppPcbaUart_Send(channel, &wake, 1u) != 0) {
        return -1;
    }
    if (recv_bytes(channel, &response, 1u, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_now_us() - started_us;
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_now_us() - started_us;
    }
    if (response_byte != 0) {
        *response_byte = response;
    }
    return response == expected ? 0 : -1;
}

int AppPcbaUart_WakeAll(uint8_t expected, uint32_t timeout_ms)
{
    uint8_t wake = PCBA_WAKE_BYTE;

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        uint8_t response = 0u;
        if (AppPcbaUart_Send(ch, &wake, 1u) != 0) {
            return -1;
        }
        if (recv_bytes(ch, &response, 1u, timeout_ms) != 0) {
            return -1;
        }
        if (response != expected) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_SendCommandRoute(uint8_t route_channel,
                                 uint8_t frame_channel,
                                 uint8_t cmd,
                                 PcbaFrame *response,
                                 uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    size_t len;

    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    len = PcbaProtocol_BuildNoData(cmd, frame_channel, frame, sizeof(frame));
    if (len == 0u || AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    return receive_frame(route_channel, response, timeout_ms);
}

int AppPcbaUart_SendCommandOne(uint8_t channel, uint8_t cmd, PcbaFrame *response, uint32_t timeout_ms)
{
    return AppPcbaUart_SendCommandRoute(channel, channel, cmd, response, timeout_ms);
}

int AppPcbaUart_SendCommandAll(uint8_t cmd, PcbaFrame *responses, uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    PcbaFrame local_responses[APP_PCBA_CHANNEL_COUNT];

    if (responses == 0) {
        responses = local_responses;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildNoData(cmd, ch, frame, sizeof(frame));
        if (len == 0u || AppPcbaUart_Send(ch, frame, (uint16_t)len) != 0) {
            return -1;
        }
        if (receive_frame(ch, &responses[ch - 1u], timeout_ms) != 0) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_SendPressureRoute(uint8_t route_channel,
                                  uint8_t frame_channel,
                                  uint8_t cmd,
                                  uint32_t pressure_001mmhg,
                                  PcbaFrame *response,
                                  uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    size_t len;

    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    len = PcbaProtocol_BuildPressure(cmd, frame_channel, pressure_001mmhg, frame, sizeof(frame));
    if (len == 0u || AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    return receive_frame(route_channel, response, timeout_ms);
}

int AppPcbaUart_SendPressureRouteTimed(uint8_t route_channel,
                                       uint8_t frame_channel,
                                       uint8_t cmd,
                                       uint32_t pressure_001mmhg,
                                       PcbaFrame *response,
                                       uint32_t timeout_ms,
                                       uint32_t *elapsed_us)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    size_t len;
    uint32_t started_us;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    len = PcbaProtocol_BuildPressure(cmd, frame_channel, pressure_001mmhg, frame, sizeof(frame));
    if (len == 0u) {
        return -1;
    }
    started_us = dwt_now_us();
    if (AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    if (receive_frame(route_channel, response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_now_us() - started_us;
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_now_us() - started_us;
    }
    return 0;
}

int AppPcbaUart_SendRawTimed(uint8_t route_channel,
                             const uint8_t *tx,
                             uint16_t tx_len,
                             PcbaFrame *response,
                             uint32_t timeout_ms,
                             uint32_t *elapsed_us)
{
    uint32_t started_us;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response == 0 || tx == 0 || tx_len == 0u) {
        return -1;
    }
    clear_pcba_frame(response);

    started_us = dwt_now_us();
    if (AppPcbaUart_Send(route_channel, tx, tx_len) != 0) {
        return -1;
    }
    if (receive_any_response(route_channel, response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_now_us() - started_us;
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_now_us() - started_us;
    }
    return 0;
}

int AppPcbaUart_SendDataRouteTimed(uint8_t route_channel,
                                   uint8_t frame_channel,
                                   uint8_t cmd,
                                   const uint8_t *data,
                                   uint16_t data_len,
                                   PcbaFrame *response,
                                   uint32_t timeout_ms,
                                   uint32_t *elapsed_us)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    size_t len;

    len = PcbaProtocol_Build(cmd, frame_channel, data, data_len, frame, sizeof(frame));
    if (len == 0u) {
        return -1;
    }

    return AppPcbaUart_SendRawTimed(route_channel,
                                    frame,
                                    (uint16_t)len,
                                    response,
                                    timeout_ms,
                                    elapsed_us);
}

int AppPcbaUart_SendPressureOne(uint8_t channel,
                                uint8_t cmd,
                                uint32_t pressure_001mmhg,
                                PcbaFrame *response,
                                uint32_t timeout_ms)
{
    return AppPcbaUart_SendPressureRoute(channel,
                                         channel,
                                         cmd,
                                         pressure_001mmhg,
                                         response,
                                         timeout_ms);
}

int AppPcbaUart_SendPressureAll(uint8_t cmd,
                                uint32_t pressure_001mmhg,
                                PcbaFrame *responses,
                                uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    PcbaFrame local_responses[APP_PCBA_CHANNEL_COUNT];

    if (responses == 0) {
        responses = local_responses;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildPressure(cmd, ch, pressure_001mmhg, frame, sizeof(frame));
        if (len == 0u || AppPcbaUart_Send(ch, frame, (uint16_t)len) != 0) {
            return -1;
        }
        if (receive_frame(ch, &responses[ch - 1u], timeout_ms) != 0) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_SendTestRoute(uint8_t route_channel,
                              uint8_t frame_channel,
                              uint32_t *pcba_pressure_001mmhg,
                              uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    PcbaFrame response;
    size_t len;

    if (pcba_pressure_001mmhg == 0) {
        return -1;
    }
    len = PcbaProtocol_BuildNoData(PCBA_CMD_PRESSURE_TEST, frame_channel, frame, sizeof(frame));
    if (len == 0u || AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    if (receive_frame(route_channel, &response, timeout_ms) != 0) {
        return -1;
    }
    (void)frame_channel;
    if (response.cmd != PCBA_CMD_PRESSURE_TEST) {
        return -1;
    }
    return PcbaProtocol_GetU32Le(&response, pcba_pressure_001mmhg) ? 0 : -1;
}

int AppPcbaUart_SendTestRouteTimed(uint8_t route_channel,
                                   uint8_t frame_channel,
                                   uint32_t *pcba_pressure_001mmhg,
                                   uint32_t timeout_ms,
                                   uint32_t *elapsed_us)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    PcbaFrame response;
    size_t len;
    uint32_t started_us;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (pcba_pressure_001mmhg == 0) {
        return -1;
    }
    len = PcbaProtocol_BuildNoData(PCBA_CMD_PRESSURE_TEST, frame_channel, frame, sizeof(frame));
    if (len == 0u) {
        return -1;
    }
    started_us = dwt_now_us();
    if (AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    if (receive_frame(route_channel, &response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_now_us() - started_us;
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_now_us() - started_us;
    }
    (void)frame_channel;
    if (response.cmd != PCBA_CMD_PRESSURE_TEST) {
        return -1;
    }
    return PcbaProtocol_GetU32Le(&response, pcba_pressure_001mmhg) ? 0 : -1;
}

int AppPcbaUart_SendTestOne(uint8_t channel, uint32_t *pcba_pressure_001mmhg, uint32_t timeout_ms)
{
    return AppPcbaUart_SendTestRoute(channel, channel, pcba_pressure_001mmhg, timeout_ms);
}

int AppPcbaUart_SendTestAll(uint32_t *pcba_pressure_001mmhg, uint32_t timeout_ms)
{
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    PcbaFrame response;

    if (pcba_pressure_001mmhg == 0) {
        return -1;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        size_t len = PcbaProtocol_BuildNoData(PCBA_CMD_PRESSURE_TEST, ch, frame, sizeof(frame));
        if (len == 0u || AppPcbaUart_Send(ch, frame, (uint16_t)len) != 0) {
            return -1;
        }
        if (receive_frame(ch, &response, timeout_ms) != 0) {
            return -1;
        }
        if (response.cmd != PCBA_CMD_PRESSURE_TEST) {
            return -1;
        }
        if (!PcbaProtocol_GetU32Le(&response, &pcba_pressure_001mmhg[ch - 1u])) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_RequestRoute(uint8_t route_channel,
                             uint8_t frame_channel,
                             uint8_t cmd,
                             PcbaFrame *response,
                             uint32_t timeout_ms)
{
    uint8_t tx[PCBA_FRAME_MAX_SIZE];
    size_t len;

    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    len = PcbaProtocol_BuildNoData(cmd, frame_channel, tx, sizeof(tx));
    if (len == 0u || AppPcbaUart_Send(route_channel, tx, (uint16_t)len) != 0) {
        return -1;
    }
    return receive_frame(route_channel, response, timeout_ms);
}

int AppPcbaUart_RequestRouteTimed(uint8_t route_channel,
                                  uint8_t frame_channel,
                                  uint8_t cmd,
                                  PcbaFrame *response,
                                  uint32_t timeout_ms,
                                  uint32_t *elapsed_us)
{
    uint8_t tx[PCBA_FRAME_MAX_SIZE];
    size_t len;
    uint32_t started_us;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response == 0) {
        return -1;
    }
    clear_pcba_frame(response);
    len = PcbaProtocol_BuildNoData(cmd, frame_channel, tx, sizeof(tx));
    if (len == 0u) {
        return -1;
    }
    started_us = dwt_now_us();
    if (AppPcbaUart_Send(route_channel, tx, (uint16_t)len) != 0) {
        return -1;
    }
    if (receive_frame(route_channel, response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_now_us() - started_us;
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_now_us() - started_us;
    }
    return 0;
}

int AppPcbaUart_RequestOne(uint8_t channel, uint8_t cmd, PcbaFrame *response, uint32_t timeout_ms)
{
    return AppPcbaUart_RequestRoute(channel, channel, cmd, response, timeout_ms);
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

int AppPcbaUart_CheckEmptyAckAll(const PcbaFrame *responses)
{
    if (responses == 0) {
        return -1;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        if (!PcbaProtocol_IsEmptyAck(&responses[ch - 1u], ch)) {
            return -1;
        }
    }

    return 0;
}

int AppPcbaUart_CheckOneByteAckAll(const PcbaFrame *responses, uint8_t expected)
{
    if (responses == 0) {
        return -1;
    }

    for (uint8_t ch = 1u; ch <= APP_PCBA_CHANNEL_COUNT; ++ch) {
        if (!PcbaProtocol_IsOneByteAck(&responses[ch - 1u], ch, expected)) {
            return -1;
        }
    }

    return 0;
}
