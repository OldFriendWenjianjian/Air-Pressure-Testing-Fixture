#ifndef LT768_BASIC_H
#define LT768_BASIC_H

#include <stdint.h>

#define LT768_SCREEN_WIDTH       1024u
#define LT768_SCREEN_HEIGHT      600u
#define LT768_COLOR_BLACK        0x00000000u
#define LT768_COLOR_WHITE        0x00FFFFFFu
#define LT768_COLOR_RED          0x00FF0000u
#define LT768_COLOR_GREEN        0x0000C060u
#define LT768_COLOR_BLUE         0x000050C8u
#define LT768_COLOR_YELLOW       0x00F0B000u
#define LT768_COLOR_GRAY         0x00808080u
#define LT768_COLOR_DARK_GRAY    0x00202020u
#define LT768_COLOR_LIGHT_GRAY   0x00E8E8E8u

void LT768_BasicInit(void);
void LT768_ShowBootText(const char *text);
void LT768_Clear(uint32_t color);
void LT768_DrawText(uint16_t x, uint16_t y, uint32_t font_color, uint32_t background_color, const char *text);
void LT768_FillRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);

#endif
