#include "lt768_port.h"
#include "board_pins.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

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

static uint8_t transfer(uint8_t byte)
{
    uint8_t rx = 0u;

    (void)HAL_SPI_TransmitReceive(&hspi2, &byte, &rx, 1u, 100u);
    return rx;
}

int LT768_PortInit(void)
{
    LT768_Reset();
    return 0;
}

void LT768_Reset(void)
{
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20u);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120u);
}

void LT768_WriteCommand(uint8_t cmd)
{
    lcd_cs_low();
    (void)transfer(LT768_SPI_WRITE_CMD_PREFIX);
    (void)transfer(cmd);
    lcd_cs_high();
}

void LT768_WriteData(uint8_t data)
{
    lcd_cs_low();
    (void)transfer(LT768_SPI_WRITE_DATA_PREFIX);
    (void)transfer(data);
    lcd_cs_high();
}

uint8_t LT768_ReadStatus(void)
{
    uint8_t status;

    lcd_cs_low();
    (void)transfer(LT768_SPI_READ_STATUS_PREFIX);
    status = transfer(0xFFu);
    lcd_cs_high();
    return status;
}

uint8_t LT768_ReadData(void)
{
    uint8_t data;

    lcd_cs_low();
    (void)transfer(LT768_SPI_READ_DATA_PREFIX);
    data = transfer(0xFFu);
    lcd_cs_high();
    return data;
}
