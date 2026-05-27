#include "app_pressure.h"
#include "app_config.h"
#include "board_pins.h"
#include "main.h"

static uint32_t s_counts[APP_PRESSURE_SENSOR_COUNT];
static uint32_t s_pressure_001mmhg[APP_PRESSURE_SENSOR_COUNT];
static uint32_t s_stable_since[APP_PRESSURE_SENSOR_COUNT];
static uint32_t s_stable_target[APP_PRESSURE_SENSOR_COUNT];
static uint8_t s_status[APP_PRESSURE_SENSOR_COUNT];
static uint8_t s_valid[APP_PRESSURE_SENSOR_COUNT];

static void i2c_delay(void)
{
    for (volatile uint32_t i = 0u; i < 90u; ++i) {
        __NOP();
    }
}

static void i2c_release(const BoardGpio *gpio)
{
    GPIO_InitTypeDef init = {0};

    init.Pin = gpio->pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(gpio->port, &init);
}

static void i2c_drive_low(const BoardGpio *gpio)
{
    GPIO_InitTypeDef init = {0};

    HAL_GPIO_WritePin(gpio->port, gpio->pin, GPIO_PIN_RESET);
    init.Pin = gpio->pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(gpio->port, &init);
}

static GPIO_PinState i2c_read_sda(uint8_t bus)
{
    return HAL_GPIO_ReadPin(BOARD_I2C_SDA_PINS[bus].port, BOARD_I2C_SDA_PINS[bus].pin);
}

static void i2c_scl_release(uint8_t bus)
{
    i2c_release(&BOARD_I2C_SCL_PINS[bus]);
}

static void i2c_scl_low(uint8_t bus)
{
    i2c_drive_low(&BOARD_I2C_SCL_PINS[bus]);
}

static void i2c_sda_release(uint8_t bus)
{
    i2c_release(&BOARD_I2C_SDA_PINS[bus]);
}

static void i2c_sda_low(uint8_t bus)
{
    i2c_drive_low(&BOARD_I2C_SDA_PINS[bus]);
}

static void i2c_start(uint8_t bus)
{
    i2c_sda_release(bus);
    i2c_scl_release(bus);
    i2c_delay();
    i2c_sda_low(bus);
    i2c_delay();
    i2c_scl_low(bus);
    i2c_delay();
}

static void i2c_stop(uint8_t bus)
{
    i2c_sda_low(bus);
    i2c_delay();
    i2c_scl_release(bus);
    i2c_delay();
    i2c_sda_release(bus);
    i2c_delay();
}

static void i2c_write_bit(uint8_t bus, uint8_t bit)
{
    if (bit != 0u) {
        i2c_sda_release(bus);
    } else {
        i2c_sda_low(bus);
    }

    i2c_delay();
    i2c_scl_release(bus);
    i2c_delay();
    i2c_scl_low(bus);
    i2c_delay();
}

static uint8_t i2c_read_bit(uint8_t bus)
{
    uint8_t bit;

    i2c_sda_release(bus);
    i2c_delay();
    i2c_scl_release(bus);
    i2c_delay();
    bit = i2c_read_sda(bus) == GPIO_PIN_SET ? 1u : 0u;
    i2c_scl_low(bus);
    i2c_delay();

    return bit;
}

static uint8_t i2c_write_byte(uint8_t bus, uint8_t value)
{
    for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1) {
        i2c_write_bit(bus, (value & mask) != 0u ? 1u : 0u);
    }

    return i2c_read_bit(bus) == 0u;
}

static uint8_t i2c_read_byte(uint8_t bus, uint8_t ack)
{
    uint8_t value = 0u;

    for (uint8_t i = 0u; i < 8u; ++i) {
        value = (uint8_t)((value << 1) | i2c_read_bit(bus));
    }
    i2c_write_bit(bus, ack == 0u ? 1u : 0u);

    return value;
}

static int mprls_send_measure_command(uint8_t bus)
{
    uint8_t ok = 1u;

    i2c_start(bus);
    ok &= i2c_write_byte(bus, (uint8_t)(APP_MPRLS_I2C_ADDRESS << 1));
    ok &= i2c_write_byte(bus, 0xAAu);
    ok &= i2c_write_byte(bus, 0x00u);
    ok &= i2c_write_byte(bus, 0x00u);
    i2c_stop(bus);

    return ok != 0u ? 0 : -1;
}

static int mprls_read_measurement(uint8_t bus, uint8_t *status, uint32_t *counts)
{
    uint8_t rx_status;
    uint32_t rx_counts;
    uint8_t ok = 1u;

    if (status == 0 || counts == 0) {
        return -1;
    }

    i2c_start(bus);
    ok &= i2c_write_byte(bus, (uint8_t)((APP_MPRLS_I2C_ADDRESS << 1) | 1u));
    if (ok == 0u) {
        i2c_stop(bus);
        return -1;
    }

    rx_status = i2c_read_byte(bus, 1u);
    rx_counts = ((uint32_t)i2c_read_byte(bus, 1u) << 16);
    rx_counts |= ((uint32_t)i2c_read_byte(bus, 1u) << 8);
    rx_counts |= i2c_read_byte(bus, 0u);
    i2c_stop(bus);

    *status = rx_status;
    *counts = rx_counts;

    return 0;
}

static uint32_t counts_to_001mmhg(uint32_t counts)
{
    uint32_t span = APP_MPRLS_OUTPUT_MAX_COUNTS - APP_MPRLS_OUTPUT_MIN_COUNTS;

    if (counts <= APP_MPRLS_OUTPUT_MIN_COUNTS) {
        return 0u;
    }
    if (counts >= APP_MPRLS_OUTPUT_MAX_COUNTS || span == 0u) {
        return APP_MPRLS_PRESSURE_MAX_001MMHG;
    }

    return (uint32_t)((((uint64_t)(counts - APP_MPRLS_OUTPUT_MIN_COUNTS) *
                        APP_MPRLS_PRESSURE_MAX_001MMHG) + (span / 2u)) / span);
}

static uint8_t status_ok(uint8_t status)
{
    return ((status & 0x40u) != 0u) &&
           ((status & 0x20u) == 0u) &&
           ((status & 0x04u) == 0u) &&
           ((status & 0x01u) == 0u);
}

void AppPressure_Init(void)
{
    GPIO_InitTypeDef init = {0};

    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    for (uint8_t i = 0; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        s_counts[i] = 0u;
        s_pressure_001mmhg[i] = 0u;
        s_stable_since[i] = 0u;
        s_stable_target[i] = 0u;
        s_status[i] = 0u;
        s_valid[i] = 0u;

        init.Pin = BOARD_I2C_SCL_PINS[i].pin;
        HAL_GPIO_Init(BOARD_I2C_SCL_PINS[i].port, &init);
        init.Pin = BOARD_I2C_SDA_PINS[i].pin;
        HAL_GPIO_Init(BOARD_I2C_SDA_PINS[i].port, &init);
    }
}

void AppPressure_Task(void)
{
    uint8_t requested[APP_PRESSURE_SENSOR_COUNT];

    for (uint8_t i = 0u; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        requested[i] = mprls_send_measure_command(i) == 0 ? 1u : 0u;
    }

    HAL_Delay(APP_MPRLS_CONVERSION_DELAY_MS);

    for (uint8_t i = 0u; i < APP_PRESSURE_SENSOR_COUNT; ++i) {
        uint8_t status = 0u;
        uint32_t counts = 0u;

        if (requested[i] == 0u || mprls_read_measurement(i, &status, &counts) != 0) {
            s_valid[i] = 0u;
            continue;
        }

        s_status[i] = status;
        s_counts[i] = counts;
        if (status_ok(status)) {
            s_pressure_001mmhg[i] = counts_to_001mmhg(counts);
            s_valid[i] = 1u;
        } else {
            s_valid[i] = 0u;
        }
    }
}

uint32_t AppPressure_GetRaw(PressureSensorIndex index)
{
    if ((uint8_t)index >= APP_PRESSURE_SENSOR_COUNT) {
        return 0u;
    }
    return s_counts[(uint8_t)index];
}

uint32_t AppPressure_Get001mmHg(PressureSensorIndex index)
{
    if ((uint8_t)index >= APP_PRESSURE_SENSOR_COUNT || s_valid[(uint8_t)index] == 0u) {
        return 0u;
    }

    return s_pressure_001mmhg[(uint8_t)index];
}

int AppPressure_IsValid(PressureSensorIndex index)
{
    if ((uint8_t)index >= APP_PRESSURE_SENSOR_COUNT) {
        return 0;
    }
    return s_valid[(uint8_t)index] != 0u;
}

int AppPressure_IsStable(PressureSensorIndex index, uint32_t target_001mmhg)
{
    uint8_t sensor = (uint8_t)index;

    if (!AppPressure_IsValid(index)) {
        if (sensor < APP_PRESSURE_SENSOR_COUNT) {
            s_stable_since[sensor] = 0u;
        }
        return 0;
    }

    uint32_t measured = AppPressure_Get001mmHg(index);
    uint32_t delta = measured > target_001mmhg ? measured - target_001mmhg : target_001mmhg - measured;

    if (delta > ((uint32_t)APP_PRESSURE_STABLE_WINDOW_RAW * APP_PRESSURE_SCALE_PER_MMHG)) {
        s_stable_since[sensor] = 0u;
        return 0;
    }

    if (s_stable_since[sensor] == 0u || s_stable_target[sensor] != target_001mmhg) {
        s_stable_since[sensor] = HAL_GetTick();
        s_stable_target[sensor] = target_001mmhg;
    }

    return (HAL_GetTick() - s_stable_since[sensor]) >= APP_PRESSURE_STABLE_TIME_MS;
}

int AppPressure_AllChannelOutputsNear(uint32_t target_001mmhg)
{
    for (uint8_t i = PRESSURE_SENSOR_CH1; i <= PRESSURE_SENSOR_CH8; ++i) {
        if (!AppPressure_IsStable((PressureSensorIndex)i, target_001mmhg)) {
            return 0;
        }
    }

    return 1;
}
