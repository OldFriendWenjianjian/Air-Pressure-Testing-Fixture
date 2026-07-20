#include "app_pcba_uart.h"
#include "app_config.h"
#include "app_pcba_rx_stream.h"
#include "app_pcba_soft_uart_logic.h"
#include "main.h"
#include "stm32f1xx_hal.h"

typedef struct {
    GPIO_TypeDef *tx_port;
    uint16_t tx_pin;
    GPIO_TypeDef *rx_port;
    uint16_t rx_pin;
} SoftUartPin;

static const SoftUartPin s_soft_uart_pins[APP_PCBA_CHANNEL_COUNT] = {
    {GPIOA, GPIO_PIN_9, GPIOA, GPIO_PIN_10},
    {GPIOD, GPIO_PIN_5, GPIOD, GPIO_PIN_6},
    {GPIOD, GPIO_PIN_8, GPIOD, GPIO_PIN_9},
    {GPIOC, GPIO_PIN_10, GPIOC, GPIO_PIN_11},
    {GPIOC, GPIO_PIN_12, GPIOD, GPIO_PIN_2},
    {GPIOE, GPIO_PIN_0, GPIOE, GPIO_PIN_1},
    {GPIOE, GPIO_PIN_2, GPIOE, GPIO_PIN_3},
    {GPIOE, GPIO_PIN_4, GPIOE, GPIO_PIN_5}
};

static AppPcbaSoftUartRxBuffer s_rx_buffers[APP_PCBA_CHANNEL_COUNT];
static AppPcbaUartDiagnostics s_uart_diagnostics[APP_PCBA_CHANNEL_COUNT];
static uint8_t s_response_capture_attempted[APP_PCBA_CHANNEL_COUNT];
static uint8_t s_uart_initialized;
static uint8_t s_pcba_lines_powered;

#define APP_PCBA_DWT_STALL_READ_LIMIT 4096u
#define APP_PCBA_DWT_WAIT_SPIN_LIMIT  4096u

static int soft_uart_capture_burst(uint8_t channel,
                                   uint8_t *bytes,
                                   uint8_t capacity,
                                   uint32_t timeout_ms,
                                   uint8_t interrupts_locked,
                                   uint32_t caller_primask);
static uint8_t soft_uart_read_byte(const SoftUartPin *pin,
                                   uint32_t start_cycle,
                                   uint32_t bit_cycles,
                                   uint8_t *value,
                                   uint32_t *stop_sample_cycle);

static void configure_pcba_uart_pins(uint8_t powered)
{
    GPIO_InitTypeDef gpio = {0};

    for (uint8_t channel = 0u; channel < APP_PCBA_CHANNEL_COUNT; ++channel) {
        const SoftUartPin *pin = &s_soft_uart_pins[channel];

        if (powered != 0u) {
            HAL_GPIO_WritePin(pin->tx_port, pin->tx_pin, GPIO_PIN_SET);
            gpio.Mode = GPIO_MODE_OUTPUT_PP;
            gpio.Pull = GPIO_NOPULL;
            gpio.Speed = GPIO_SPEED_FREQ_HIGH;
            gpio.Pin = pin->tx_pin;
            HAL_GPIO_Init(pin->tx_port, &gpio);

            gpio.Mode = GPIO_MODE_INPUT;
            gpio.Pull = GPIO_PULLUP;
            gpio.Pin = pin->rx_pin;
            HAL_GPIO_Init(pin->rx_port, &gpio);
        } else {
            gpio.Mode = GPIO_MODE_ANALOG;
            gpio.Pull = GPIO_NOPULL;
            gpio.Speed = GPIO_SPEED_FREQ_LOW;
            gpio.Pin = pin->tx_pin;
            HAL_GPIO_Init(pin->tx_port, &gpio);
            gpio.Pin = pin->rx_pin;
            HAL_GPIO_Init(pin->rx_port, &gpio);
        }
    }
}

static uint32_t dwt_now_cycles(void)
{
    return DWT->CYCCNT;
}

static uint32_t dwt_elapsed_us(uint32_t started_cycles)
{
    uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000u;

    if (cycles_per_us == 0u) {
        return 0u;
    }
    return (DWT->CYCCNT - started_cycles) / cycles_per_us;
}

static void restore_interrupt_mask(uint32_t primask)
{
    if (primask == 0u) {
        __enable_irq();
    }
}

static uint8_t dwt_enable_and_verify(void)
{
    uint32_t before;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    before = DWT->CYCCNT;
    for (volatile uint32_t i = 0u; i < 32u; ++i) {
        __NOP();
    }
    return DWT->CYCCNT != before ? 1u : 0u;
}

__STATIC_FORCEINLINE uint8_t dwt_wait_until(uint32_t target_cycle)
{
    uint32_t spin_count = 0u;

    while ((int32_t)(DWT->CYCCNT - target_cycle) < 0) {
        ++spin_count;
        if (spin_count >= APP_PCBA_DWT_WAIT_SPIN_LIMIT) {
            return 0u;
        }
    }
    return 1u;
}

__STATIC_FORCEINLINE uint8_t soft_uart_rx_is_high(const SoftUartPin *pin)
{
    return (pin->rx_port->IDR & pin->rx_pin) != 0u ? 1u : 0u;
}

__STATIC_FORCEINLINE void soft_uart_tx_write(const SoftUartPin *pin, uint8_t high)
{
    if (high != 0u) {
        pin->tx_port->BSRR = pin->tx_pin;
    } else {
        pin->tx_port->BRR = pin->tx_pin;
    }
}

static int send_soft_uart(uint8_t channel, const uint8_t *data, uint16_t len)
{
    uint8_t response_burst[PCBA_FRAME_MAX_SIZE];
    uint32_t bit_cycles;
    uint32_t capture_timeout_ms = APP_PCBA_SOFT_UART_MAX_WAIT_MS;
    uint32_t deadline;
    uint32_t primask;
    const SoftUartPin *pin;

    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        data == 0 || len == 0u) {
        return -1;
    }

    pin = &s_soft_uart_pins[channel - 1u];
    bit_cycles = HAL_RCC_GetHCLKFreq() / APP_PCBA_UART_BAUDRATE;
    if (bit_cycles == 0u || dwt_enable_and_verify() == 0u) {
        return -1;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    deadline = DWT->CYCCNT;
    for (uint16_t i = 0u; i < len; ++i) {
        uint8_t b = data[i];

        soft_uart_tx_write(pin, 0u);
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            deadline += bit_cycles;
            if (dwt_wait_until(deadline) == 0u) {
                soft_uart_tx_write(pin, 1u);
                restore_interrupt_mask(primask);
                ++s_uart_diagnostics[channel - 1u].timeout_count;
                return -1;
            }
            soft_uart_tx_write(pin, (uint8_t)((b >> bit) & 0x01u));
        }
        deadline += bit_cycles;
        if (dwt_wait_until(deadline) == 0u) {
            soft_uart_tx_write(pin, 1u);
            restore_interrupt_mask(primask);
            ++s_uart_diagnostics[channel - 1u].timeout_count;
            return -1;
        }
        soft_uart_tx_write(pin, 1u);
        deadline += bit_cycles;
        if ((i + 1u) < len && dwt_wait_until(deadline) == 0u) {
            restore_interrupt_mask(primask);
            ++s_uart_diagnostics[channel - 1u].timeout_count;
            return -1;
        }
    }

    if (len == 1u && data[0] == PCBA_WAKE_BYTE) {
        const uint32_t wake_wait_started = DWT->CYCCNT;
        const uint32_t wake_wait_cycles = bit_cycles * 10u;
        uint32_t spin_count = 0u;

        while (soft_uart_rx_is_high(pin) != 0u &&
               (DWT->CYCCNT - wake_wait_started) < wake_wait_cycles) {
            ++spin_count;
            if (spin_count >= APP_PCBA_DWT_WAIT_SPIN_LIMIT) {
                soft_uart_tx_write(pin, 1u);
                restore_interrupt_mask(primask);
                ++s_uart_diagnostics[channel - 1u].timeout_count;
                return -1;
            }
        }
        if (soft_uart_rx_is_high(pin) == 0u) {
            uint8_t wake_response = 0u;
            uint32_t stop_sample_cycle;
            const uint32_t byte_start = DWT->CYCCNT;

            if (soft_uart_read_byte(pin,
                                    byte_start,
                                    bit_cycles,
                                    &wake_response,
                                    &stop_sample_cycle) != 0u) {
                restore_interrupt_mask(primask);
                s_response_capture_attempted[channel - 1u] = 1u;
                if (AppPcbaSoftUartRxBuffer_Load(&s_rx_buffers[channel - 1u],
                                                 &wake_response,
                                                 1u) == 0u) {
                    ++s_uart_diagnostics[channel - 1u].buffer_overflow_count;
                }
                ++s_uart_diagnostics[channel - 1u].burst_count;
                ++s_uart_diagnostics[channel - 1u].byte_count;
                return 0;
            }
            ++s_uart_diagnostics[channel - 1u].framing_error_count;
        }
        if (capture_timeout_ms > 1u) {
            --capture_timeout_ms;
        }
    }

    {
        const int response_len = soft_uart_capture_burst(
            channel,
            response_burst,
            sizeof(response_burst),
            capture_timeout_ms,
            1u,
            primask);
        s_response_capture_attempted[channel - 1u] = 1u;
        if (response_len > 0 &&
            AppPcbaSoftUartRxBuffer_Load(&s_rx_buffers[channel - 1u],
                                         response_burst,
                                         (uint8_t)response_len) == 0u) {
            ++s_uart_diagnostics[channel - 1u].buffer_overflow_count;
        }
    }

    return 0;
}

static uint8_t soft_uart_read_byte(const SoftUartPin *pin,
                                   uint32_t start_cycle,
                                   uint32_t bit_cycles,
                                   uint8_t *value,
                                   uint32_t *stop_sample_cycle)
{
    uint32_t sample_cycle = start_cycle + (bit_cycles / 2u);
    uint8_t byte = 0u;

    if (dwt_wait_until(sample_cycle) == 0u) {
        return 0u;
    }
    if (soft_uart_rx_is_high(pin) != 0u) {
        return 0u;
    }

    sample_cycle += bit_cycles;
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        if (dwt_wait_until(sample_cycle) == 0u) {
            return 0u;
        }
        if (soft_uart_rx_is_high(pin) != 0u) {
            byte |= (uint8_t)(1u << bit);
        }
        sample_cycle += bit_cycles;
    }

    if (dwt_wait_until(sample_cycle) == 0u) {
        return 0u;
    }
    if (soft_uart_rx_is_high(pin) == 0u) {
        return 0u;
    }

    *value = byte;
    if (stop_sample_cycle != 0) {
        *stop_sample_cycle = sample_cycle;
    }
    return 1u;
}

static int soft_uart_capture_burst(uint8_t channel,
                                   uint8_t *bytes,
                                   uint8_t capacity,
                                   uint32_t timeout_ms,
                                   uint8_t interrupts_locked,
                                   uint32_t caller_primask)
{
    const SoftUartPin *pin;
    AppPcbaUartDiagnostics *diagnostics;
    uint32_t bit_cycles;
    uint32_t wait_cycles;
    uint32_t wait_started;
    uint32_t byte_start;
    uint32_t stop_sample_cycle;
    uint32_t primask;
    uint32_t previous_cycle;
    uint32_t stalled_reads = 0u;
    uint8_t count = 0u;
    uint8_t require_idle_high = 0u;

    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        bytes == 0 || capacity == 0u) {
        if (interrupts_locked != 0u) {
            restore_interrupt_mask(caller_primask);
        }
        return -1;
    }
    pin = &s_soft_uart_pins[channel - 1u];
    diagnostics = &s_uart_diagnostics[channel - 1u];
    timeout_ms = AppPcbaSoftUart_ClampTimeoutMs(timeout_ms);
    bit_cycles = HAL_RCC_GetHCLKFreq() / APP_PCBA_UART_BAUDRATE;
    wait_cycles = (HAL_RCC_GetHCLKFreq() / 1000u) * timeout_ms;
    if (bit_cycles == 0u ||
        (interrupts_locked == 0u && dwt_enable_and_verify() == 0u)) {
        ++diagnostics->timeout_count;
        if (interrupts_locked != 0u) {
            restore_interrupt_mask(caller_primask);
        }
        return -1;
    }
    if (interrupts_locked == 0u) {
        caller_primask = __get_PRIMASK();
        __disable_irq();
        interrupts_locked = 1u;
    }
    wait_started = DWT->CYCCNT;
    previous_cycle = wait_started;

acquire_start:
    if (require_idle_high != 0u) {
        while (soft_uart_rx_is_high(pin) == 0u) {
            const uint32_t current = DWT->CYCCNT;

            if (current == previous_cycle) {
                ++stalled_reads;
                if (stalled_reads >= APP_PCBA_DWT_STALL_READ_LIMIT) {
                    ++diagnostics->timeout_count;
                    restore_interrupt_mask(caller_primask);
                    return -1;
                }
            } else {
                previous_cycle = current;
                stalled_reads = 0u;
            }
            if (timeout_ms == 0u || (current - wait_started) >= wait_cycles) {
                ++diagnostics->timeout_count;
                restore_interrupt_mask(caller_primask);
                return -1;
            }
        }
        require_idle_high = 0u;
    }

    for (;;) {
        while (soft_uart_rx_is_high(pin) != 0u) {
            const uint32_t current = DWT->CYCCNT;

            if (current == previous_cycle) {
                ++stalled_reads;
                if (stalled_reads >= APP_PCBA_DWT_STALL_READ_LIMIT) {
                    ++diagnostics->timeout_count;
                    if (interrupts_locked != 0u) {
                        restore_interrupt_mask(caller_primask);
                    }
                    return -1;
                }
            } else {
                previous_cycle = current;
                stalled_reads = 0u;
            }
            if (timeout_ms == 0u || (current - wait_started) >= wait_cycles) {
                ++diagnostics->timeout_count;
                if (interrupts_locked != 0u) {
                    restore_interrupt_mask(caller_primask);
                }
                return -1;
            }
        }

        break;
    }

    primask = caller_primask;
    byte_start = DWT->CYCCNT;
    for (;;) {
        if (soft_uart_read_byte(pin,
                                byte_start,
                                bit_cycles,
                                &bytes[count],
                                &stop_sample_cycle) == 0u) {
            ++diagnostics->framing_error_count;
            if (count == 0u) {
                require_idle_high = 1u;
                previous_cycle = DWT->CYCCNT;
                stalled_reads = 0u;
                goto acquire_start;
            }
            break;
        }
        ++count;
        if (count >= capacity) {
            if (soft_uart_rx_is_high(pin) == 0u) {
                ++diagnostics->buffer_overflow_count;
            }
            break;
        }

        {
            const uint32_t interbyte_deadline = stop_sample_cycle + (3u * bit_cycles);
            uint8_t start_detected = 0u;

            previous_cycle = DWT->CYCCNT;
            stalled_reads = 0u;
            while ((int32_t)(DWT->CYCCNT - interbyte_deadline) < 0) {
                const uint32_t current = DWT->CYCCNT;

                if (soft_uart_rx_is_high(pin) == 0u) {
                    byte_start = DWT->CYCCNT;
                    start_detected = 1u;
                    break;
                }
                if (current == previous_cycle) {
                    ++stalled_reads;
                    if (stalled_reads >= APP_PCBA_DWT_STALL_READ_LIMIT) {
                        ++diagnostics->framing_error_count;
                        restore_interrupt_mask(primask);
                        return count > 0u ? (int)count : -1;
                    }
                } else {
                    previous_cycle = current;
                    stalled_reads = 0u;
                }
            }
            if (start_detected == 0u) {
                break;
            }
        }
    }
    restore_interrupt_mask(primask);

    if (count == 0u) {
        return -1;
    }
    ++diagnostics->burst_count;
    diagnostics->byte_count += count;
    return (int)count;
}

static int recv_soft_uart(uint8_t channel,
                          uint8_t *data,
                          uint16_t len,
                          uint32_t timeout_ms)
{
    AppPcbaSoftUartRxBuffer *buffer;
    uint32_t started_cycles;
    uint32_t effective_timeout_ms;

    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        data == 0 || len == 0u) {
        return -1;
    }
    buffer = &s_rx_buffers[channel - 1u];
    effective_timeout_ms = AppPcbaSoftUart_ClampTimeoutMs(timeout_ms);
    started_cycles = dwt_now_cycles();

    for (uint16_t i = 0u; i < len; ++i) {
        if (AppPcbaSoftUartRxBuffer_Pop(buffer, &data[i]) == 0u) {
            if (s_response_capture_attempted[channel - 1u] != 0u) {
                return -1;
            }
            uint8_t burst[PCBA_FRAME_MAX_SIZE];
            uint32_t elapsed_us = dwt_elapsed_us(started_cycles);
            uint32_t elapsed_ms = elapsed_us / 1000u;
            uint32_t remaining_ms = elapsed_ms < effective_timeout_ms ?
                                    effective_timeout_ms - elapsed_ms : 0u;
            int burst_len = soft_uart_capture_burst(channel,
                                                     burst,
                                                     sizeof(burst),
                                                     remaining_ms,
                                                     0u,
                                                     0u);

            if (burst_len <= 0 ||
                AppPcbaSoftUartRxBuffer_Load(buffer,
                                             burst,
                                             (uint8_t)burst_len) == 0u ||
                AppPcbaSoftUartRxBuffer_Pop(buffer, &data[i]) == 0u) {
                if (burst_len > 0) {
                    ++s_uart_diagnostics[channel - 1u].buffer_overflow_count;
                }
                return -1;
            }
        }
    }

    return 0;
}

static int recv_bytes(uint8_t channel, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
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
    uint32_t started_at;
    uint32_t elapsed_ms;
    uint32_t remaining_ms;

    if (response == 0) {
        return -1;
    }
    timeout_ms = AppPcbaSoftUart_ClampTimeoutMs(timeout_ms);
    clear_pcba_frame(response);
    started_at = HAL_GetTick();
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
    elapsed_ms = HAL_GetTick() - started_at;
    remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;
    if (remaining_ms == 0u ||
        recv_bytes(channel, &rx[6], (uint16_t)(data_len + 2u), remaining_ms) != 0) {
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
    uint32_t started_at;
    uint32_t elapsed_ms;
    uint32_t remaining_ms;

    if (response == 0) {
        return -1;
    }
    timeout_ms = AppPcbaSoftUart_ClampTimeoutMs(timeout_ms);
    clear_pcba_frame(response);
    started_at = HAL_GetTick();

    if (recv_bytes(channel, &byte, 1u, timeout_ms) != 0) {
        return -1;
    }
    raw[raw_len++] = byte;

    if (byte == PCBA_FRAME_HEAD0) {
        elapsed_ms = HAL_GetTick() - started_at;
        remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;
        if (remaining_ms > 0u && recv_bytes(channel, &byte, 1u, remaining_ms) == 0) {
            raw[raw_len++] = byte;
            if (byte == PCBA_FRAME_HEAD1) {
                uint8_t head_tail[4];
                elapsed_ms = HAL_GetTick() - started_at;
                remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;
                if (remaining_ms > 0u && recv_bytes(channel, head_tail, sizeof(head_tail), remaining_ms) == 0) {
                    for (uint8_t i = 0u; i < sizeof(head_tail); ++i) {
                        raw[raw_len++] = head_tail[i];
                    }
                    uint16_t data_len = (uint16_t)(head_tail[2] | ((uint16_t)head_tail[3] << 8));
                    elapsed_ms = HAL_GetTick() - started_at;
                    remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;
                    if (data_len <= PCBA_FRAME_MAX_DATA &&
                        raw_len + data_len + 2u <= PCBA_FRAME_MAX_SIZE &&
                        remaining_ms > 0u &&
                        recv_bytes(channel, &raw[raw_len], (uint16_t)(data_len + 2u), remaining_ms) == 0) {
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

    while (raw_len < PCBA_FRAME_MAX_SIZE) {
        elapsed_ms = HAL_GetTick() - started_at;
        remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;
        if (remaining_ms == 0u ||
            recv_bytes(channel, &byte, 1u, remaining_ms < 5u ? remaining_ms : 5u) != 0) {
            break;
        }
        raw[raw_len++] = byte;
    }
    append_response_raw(response, raw, raw_len);
    return 0;
}

static void expected_response_for_command(uint8_t command,
                                          uint8_t *expected_cmd,
                                          uint16_t *expected_len)
{
    if (command == PCBA_CMD_SET_TEST_MODE) {
        *expected_cmd = PCBA_CMD_SET_TEST_MODE;
        *expected_len = 2u;
    } else if (command == PCBA_CMD_PRESSURE_TEST) {
        *expected_cmd = PCBA_CMD_PRESSURE_TEST;
        *expected_len = 4u;
    } else {
        *expected_cmd = PCBA_CMD_ACK;
        *expected_len = 1u;
    }
}

static int receive_expected_frame(uint8_t channel,
                                  PcbaFrame *response,
                                  uint32_t timeout_ms,
                                  uint8_t expected_cmd,
                                  uint16_t expected_len)
{
    AppPcbaRxStream stream;
    PcbaFrame candidate;
    uint32_t started_at;

    if (response == 0) {
        return -1;
    }
    timeout_ms = AppPcbaSoftUart_ClampTimeoutMs(timeout_ms);
    clear_pcba_frame(response);
    clear_pcba_frame(&candidate);
    AppPcbaRxStream_Init(&stream);
    started_at = HAL_GetTick();

    while ((HAL_GetTick() - started_at) < timeout_ms) {
        uint8_t byte;
        uint32_t elapsed_ms = HAL_GetTick() - started_at;
        uint32_t remaining_ms = elapsed_ms < timeout_ms ? timeout_ms - elapsed_ms : 0u;

        if (remaining_ms == 0u || recv_bytes(channel, &byte, 1u, remaining_ms) != 0) {
            break;
        }
        if (AppPcbaRxStream_Push(&stream, byte, &candidate) == 0u) {
            continue;
        }
        *response = candidate;
        if (AppPcbaRxFrame_IsExpected(&candidate, expected_cmd, expected_len) != 0u) {
            return 0;
        }
    }

    if (response->raw_len == 0u) {
        AppPcbaRxStream_CopyDiagnostic(&stream, response);
    }
    return -1;
}

static int send_expected_timed(uint8_t route_channel,
                               const uint8_t *tx,
                               uint16_t tx_len,
                               PcbaFrame *response,
                               uint32_t timeout_ms,
                               uint8_t expected_cmd,
                               uint16_t expected_len,
                               uint32_t *elapsed_us)
{
    uint32_t started_cycles;
    int rc;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response == 0 || tx == 0 || tx_len == 0u) {
        return -1;
    }
    clear_pcba_frame(response);
    if (dwt_enable_and_verify() == 0u) {
        return -1;
    }
    started_cycles = dwt_now_cycles();
    if (AppPcbaUart_Send(route_channel, tx, tx_len) != 0) {
        return -1;
    }
    rc = receive_expected_frame(route_channel,
                                response,
                                timeout_ms,
                                expected_cmd,
                                expected_len);
    if (elapsed_us != 0) {
        *elapsed_us = dwt_elapsed_us(started_cycles);
    }
    return rc;
}

void AppPcbaUart_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    s_uart_initialized = 1u;
    configure_pcba_uart_pins(s_pcba_lines_powered);
    for (uint8_t channel = 0u; channel < APP_PCBA_CHANNEL_COUNT; ++channel) {
        AppPcbaSoftUartRxBuffer_Clear(&s_rx_buffers[channel]);
        s_response_capture_attempted[channel] = 0u;
        s_uart_diagnostics[channel].burst_count = 0u;
        s_uart_diagnostics[channel].byte_count = 0u;
        s_uart_diagnostics[channel].timeout_count = 0u;
        s_uart_diagnostics[channel].framing_error_count = 0u;
        s_uart_diagnostics[channel].buffer_overflow_count = 0u;
    }
}

void AppPcbaUart_SetPowerState(uint8_t powered)
{
    powered = powered != 0u ? 1u : 0u;
    if (s_pcba_lines_powered == powered) {
        return;
    }
    s_pcba_lines_powered = powered;
    if (s_uart_initialized == 0u) {
        return;
    }
    configure_pcba_uart_pins(powered);
    for (uint8_t channel = 0u; channel < APP_PCBA_CHANNEL_COUNT; ++channel) {
        AppPcbaSoftUartRxBuffer_Clear(&s_rx_buffers[channel]);
        s_response_capture_attempted[channel] = 0u;
    }
}

int AppPcbaUart_Send(uint8_t channel, const uint8_t *data, uint16_t len)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT ||
        data == 0 || s_uart_initialized == 0u ||
        s_pcba_lines_powered == 0u) {
        return -1;
    }

    AppPcbaSoftUartRxBuffer_Clear(&s_rx_buffers[channel - 1u]);
    s_response_capture_attempted[channel - 1u] = 0u;
    return send_soft_uart(channel, data, len);
}

void AppPcbaUart_FlushRx(uint8_t channel)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT) {
        return;
    }

    AppPcbaSoftUartRxBuffer_Clear(&s_rx_buffers[channel - 1u]);
    s_response_capture_attempted[channel - 1u] = 0u;
}

int AppPcbaUart_GetDiagnostics(uint8_t channel, AppPcbaUartDiagnostics *diagnostics)
{
    if (channel < 1u || channel > APP_PCBA_CHANNEL_COUNT || diagnostics == 0) {
        return -1;
    }
    *diagnostics = s_uart_diagnostics[channel - 1u];
    return 0;
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
    uint32_t started_cycles;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response_byte != 0) {
        *response_byte = 0u;
    }
    if (dwt_enable_and_verify() == 0u) {
        return -1;
    }
    started_cycles = dwt_now_cycles();
    if (AppPcbaUart_Send(channel, &wake, 1u) != 0) {
        return -1;
    }
    if (recv_bytes(channel, &response, 1u, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_elapsed_us(started_cycles);
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_elapsed_us(started_cycles);
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

    len = PcbaProtocol_BuildPressure(cmd, frame_channel, pressure_001mmhg, frame, sizeof(frame));
    if (len == 0u) {
        return -1;
    }
    return send_expected_timed(route_channel,
                               frame,
                               (uint16_t)len,
                               response,
                               timeout_ms,
                               PCBA_CMD_ACK,
                               1u,
                               elapsed_us);
}

int AppPcbaUart_SendRawTimed(uint8_t route_channel,
                             const uint8_t *tx,
                             uint16_t tx_len,
                             PcbaFrame *response,
                             uint32_t timeout_ms,
                             uint32_t *elapsed_us)
{
    uint32_t started_cycles;

    if (elapsed_us != 0) {
        *elapsed_us = 0u;
    }
    if (response == 0 || tx == 0 || tx_len == 0u) {
        return -1;
    }
    clear_pcba_frame(response);

    if (dwt_enable_and_verify() == 0u) {
        return -1;
    }
    started_cycles = dwt_now_cycles();
    if (AppPcbaUart_Send(route_channel, tx, tx_len) != 0) {
        return -1;
    }
    if (receive_any_response(route_channel, response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_elapsed_us(started_cycles);
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_elapsed_us(started_cycles);
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
    uint8_t expected_cmd;
    uint16_t expected_len;

    len = PcbaProtocol_Build(cmd, frame_channel, data, data_len, frame, sizeof(frame));
    if (len == 0u) {
        return -1;
    }
    expected_response_for_command(cmd, &expected_cmd, &expected_len);
    return send_expected_timed(route_channel,
                               frame,
                               (uint16_t)len,
                               response,
                               timeout_ms,
                               expected_cmd,
                               expected_len,
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
    if (!PcbaProtocol_GetPressure001mmHg(&response,
                                         pcba_pressure_001mmhg)) {
        return -1;
    }
    return 0;
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
    uint32_t started_cycles;

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
    if (dwt_enable_and_verify() == 0u) {
        return -1;
    }
    started_cycles = dwt_now_cycles();
    if (AppPcbaUart_Send(route_channel, frame, (uint16_t)len) != 0) {
        return -1;
    }
    if (receive_frame(route_channel, &response, timeout_ms) != 0) {
        if (elapsed_us != 0) {
            *elapsed_us = dwt_elapsed_us(started_cycles);
        }
        return -1;
    }
    if (elapsed_us != 0) {
        *elapsed_us = dwt_elapsed_us(started_cycles);
    }
    (void)frame_channel;
    if (response.cmd != PCBA_CMD_PRESSURE_TEST) {
        return -1;
    }
    if (!PcbaProtocol_GetPressure001mmHg(&response,
                                         pcba_pressure_001mmhg)) {
        return -1;
    }
    return 0;
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
        if (!PcbaProtocol_GetPressure001mmHg(
                &response,
                &pcba_pressure_001mmhg[ch - 1u])) {
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
    uint8_t expected_cmd;
    uint16_t expected_len;

    len = PcbaProtocol_BuildNoData(cmd, frame_channel, tx, sizeof(tx));
    if (len == 0u) {
        return -1;
    }
    expected_response_for_command(cmd, &expected_cmd, &expected_len);
    return send_expected_timed(route_channel,
                               tx,
                               (uint16_t)len,
                               response,
                               timeout_ms,
                               expected_cmd,
                               expected_len,
                               elapsed_us);
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
