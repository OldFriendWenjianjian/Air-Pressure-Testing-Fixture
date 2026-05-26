/********************* COPYRIGHT  **********************
* File Name        : if_port.h
* Author           : Levetop Electronics
* Version          : V1.0
* Date             : 2017-9-11
* Description      : 选择不同的驱动接口
********************************************************/

#ifndef _if_port_h
#define _if_port_h
#include "sys.h"      // STM32的头文件


#if 0 //4wire SPI

#define SDA_IN()  {GPIOE->MODER&=~(3<<(2*7));GPIOE->MODER|=0<<2*7;}//PE7输入模式

#define CS  		PDout(14) 
#define SCL  		PDout(0)
#define SDI  		PEin(7)
#define SDO    		PDout(15)
#define RESET  		PGout(15)

#endif


#if 0 //4wire SPI

#define SDA_IN()  {GPIOC->MODER&=~(3<<(2*2));GPIOC->MODER|=0<<2*2;}//PC2输入模式

#define CS  		PCout(1) 
#define SCL  		PBout(10)
#define SDI  		PCin(2)//MISO
#define SDO    		PCout(3)//MOSI
#define RESET  		PGout(6)

#endif



#define SDA_IN()  {GPIOD->MODER&=~(3<<(2*9));GPIOC->MODER|=0<<2*9;}//PD9输入模式


#define RESET  		PGout(15)
#define CS  		PEout(15) 
#define SCL  		PDout(8)
#define SDI  		PDin(9)//MISO
#define SDO    		PDout(10)//MOSI





void Parallel_Init(void);
void SPI_IO_Init(void);

void LCD_CmdWrite(u8 cmd);
void LCD_DataWrite(u8 data);
void LCD_DataWrite_Pixel(u16 data);
u8 LCD_StatusRead(void);
u16 LCD_DataRead(void);
	 
void Delay_us(u16 time); //延时函数us级
void Delay_ms(u16 time); //延时函数ms级

void test_SPIIO(void);



#endif
