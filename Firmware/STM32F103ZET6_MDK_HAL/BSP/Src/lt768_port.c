#include "lt768_port.h"
#include "board_pins.h"
#include "main.h"

#define LT768_SPI_WRITE_CMD_PREFIX      0x00u
#define LT768_SPI_WRITE_DATA_PREFIX     0x80u
#define LT768_SPI_READ_STATUS_PREFIX    0x40u
#define LT768_SPI_READ_DATA_PREFIX      0xC0u

static void lcd_cs_low(void)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
}

static void lcd_cs_high(void)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static void lcd_sclk_low(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
}

static void lcd_sclk_high(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
}

static void lcd_mosi_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, state);
}

static GPIO_PinState lcd_miso_read(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
}

static void lt768_spi_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &gpio);

    lcd_sclk_low();
    lcd_mosi_write(GPIO_PIN_RESET);
}

static void lt768_spi_write_byte(uint8_t value)
{
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        lcd_sclk_low();
        lcd_mosi_write((value & 0x80u) != 0u ? GPIO_PIN_SET : GPIO_PIN_RESET);
        __NOP();
        lcd_sclk_high();
        value <<= 1;
    }

    lcd_sclk_low();
    lcd_mosi_write(GPIO_PIN_RESET);
}

static uint8_t lt768_spi_read_byte(void)
{
    uint8_t value = 0u;

    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        value <<= 1;
        /*
         * Match the vendor bit-bang timing: data is sampled while SCLK is high,
         * then the clock returns low for the next bit. The previous low-sample-
         * high sequence made LT768 register/status reads unreliable after reset.
         */
        lcd_sclk_high();
        __NOP();
        __NOP();
        if (lcd_miso_read() == GPIO_PIN_SET) {
            value |= 0x01u;
        }
        lcd_sclk_low();
    }

    lcd_sclk_low();
    return value;
}

int LT768_PortInit(void)
{
    lt768_spi_gpio_init();
    /* Match the vendor bring-up: allow the module power rails to settle
       before toggling the LT768 reset line. */
    HAL_Delay(300u);
    LT768_Reset();
    return 0;
}

void LT768_Reset(void)
{
    /*
     * Match the vendor panel driver reset pulse more closely:
     * drive reset high briefly, assert low, then release high again.
     * This guarantees a fresh falling-edge reset even if the line was already
     * low before the LCD rail really came up.
     */
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10u);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(50u);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120u);
}

void LT768_WriteCommand(uint8_t cmd)
{
    lcd_cs_low();
    lt768_spi_write_byte(LT768_SPI_WRITE_CMD_PREFIX);
    lt768_spi_write_byte(cmd);
    lcd_cs_high();
}

void LT768_WriteData(uint8_t data)
{
    lcd_cs_low();
    lt768_spi_write_byte(LT768_SPI_WRITE_DATA_PREFIX);
    lt768_spi_write_byte(data);
    lcd_cs_high();
}

uint8_t LT768_ReadStatus(void)
{
    uint8_t status;

    lcd_cs_low();
    lt768_spi_write_byte(LT768_SPI_READ_STATUS_PREFIX);
    status = lt768_spi_read_byte();
    lcd_cs_high();
    return status;
}

uint8_t LT768_ReadData(void)
{
    uint8_t data;

    lcd_cs_low();
    lt768_spi_write_byte(LT768_SPI_READ_DATA_PREFIX);
    data = lt768_spi_read_byte();
    lcd_cs_high();
    return data;
}
