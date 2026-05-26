#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f1xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} BoardGpio;

extern const BoardGpio BOARD_VALVE_PINS[26];
extern const BoardGpio BOARD_I2C_SCL_PINS[14];
extern const BoardGpio BOARD_I2C_SDA_PINS[14];

#define KEY1_GPIO_Port                 GPIOC
#define KEY1_Pin                       GPIO_PIN_3
#define KEY2_GPIO_Port                 GPIOC
#define KEY2_Pin                       GPIO_PIN_4
#define KEY3_GPIO_Port                 GPIOC
#define KEY3_Pin                       GPIO_PIN_5
#define KEY4_GPIO_Port                 GPIOC
#define KEY4_Pin                       GPIO_PIN_7
#define PRESS_SWITCH_GPIO_Port         GPIOC
#define PRESS_SWITCH_Pin               GPIO_PIN_8

#define PCBA_5V_ENABLE_GPIO_Port       GPIOC
#define PCBA_5V_ENABLE_Pin             GPIO_PIN_9
#define PCBA_45V_ENABLE_GPIO_Port      GPIOC
#define PCBA_45V_ENABLE_Pin            GPIO_PIN_13
#define PCBA_50MA_ENABLE_GPIO_Port     GPIOB
#define PCBA_50MA_ENABLE_Pin           GPIO_PIN_1

#define LCD_CS_GPIO_Port               GPIOB
#define LCD_CS_Pin                     GPIO_PIN_12
#define LCD_RST_GPIO_Port              GPIOG
#define LCD_RST_Pin                    GPIO_PIN_0
#define LCD_INT_GPIO_Port              GPIOG
#define LCD_INT_Pin                    GPIO_PIN_1
#define CTP_INT_GPIO_Port              GPIOG
#define CTP_INT_Pin                    GPIO_PIN_2
#define CTP_RST_GPIO_Port              GPIOG
#define CTP_RST_Pin                    GPIO_PIN_3

#define W25Q_CS_GPIO_Port              GPIOG
#define W25Q_CS_Pin                    GPIO_PIN_4

void BoardPins_EnableAllGpioClocks(void);
void BoardPins_ConfigOutputsSafe(void);
void BoardPins_ConfigKeys(void);
void BoardPins_ConfigSpi3Float(void);
void BoardPins_ConfigSpi3Msc(void);

#endif
