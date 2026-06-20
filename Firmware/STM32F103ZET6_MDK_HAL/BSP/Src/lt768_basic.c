#include "lt768_basic.h"
#include "lt768_port.h"
#include "main.h"

#define LT768_BIT(n)                 ((uint8_t)(1u << (n)))
#define LT768_LCD_VBPD               23u
#define LT768_LCD_VFPD               22u
#define LT768_LCD_VSPW               1u
#define LT768_LCD_HBPD               46u
#define LT768_LCD_HFPD               210u
#define LT768_LCD_HSPW               1u

static uint8_t s_lt768_ready;
volatile uint8_t g_lt768_diagnostics[32];

static void reg_write(uint8_t reg, uint8_t data)
{
    LT768_WriteCommand(reg);
    LT768_WriteData(data);
}

static uint8_t reg_read(uint8_t reg)
{
    LT768_WriteCommand(reg);
    return LT768_ReadData();
}

static void reg_set_bits(uint8_t reg, uint8_t set_mask, uint8_t clear_mask)
{
    uint8_t value = reg_read(reg);
    value &= (uint8_t)~clear_mask;
    value |= set_mask;
    reg_write(reg, value);
}

static void wait_2d_idle(void)
{
    uint32_t start = HAL_GetTick();
    while ((LT768_ReadStatus() & 0x08u) != 0u) {
        if ((HAL_GetTick() - start) > 200u) {
            break;
        }
    }
}

static void wait_sdram_ready(void)
{
    uint32_t start = HAL_GetTick();
    while ((LT768_ReadStatus() & 0x04u) == 0u) {
        if ((HAL_GetTick() - start) > 200u) {
            break;
        }
    }
}

static void wait_mem_write_fifo_not_full(void)
{
    uint32_t start = HAL_GetTick();
    while ((LT768_ReadStatus() & 0x80u) != 0u) {
        if ((HAL_GetTick() - start) > 50u) {
            break;
        }
    }
}

static void graphic_mode(void)
{
    reg_set_bits(0x03u, 0u, LT768_BIT(2));
}

static void text_mode(void)
{
    reg_set_bits(0x03u, LT768_BIT(2), 0u);
}

static void select_main_window_24bpp(void)
{
    uint8_t value = reg_read(0x10u);
    value |= LT768_BIT(3);
    reg_write(0x10u, value);
}

static void panel_init_1024x600(void)
{
    uint8_t value;
    const uint16_t sdram_itv = 0x031Au;

    reg_write(0x05u, (uint8_t)((2u << 6) | (5u << 1)));
    reg_write(0x07u, (uint8_t)((2u << 6) | (5u << 1)));
    reg_write(0x09u, (uint8_t)((2u << 6) | (5u << 1)));
    reg_write(0x06u, 65u);
    reg_write(0x08u, 100u);
    reg_write(0x0Au, 100u);
    reg_write(0x00u, 0x80u);
    HAL_Delay(2u);

    reg_write(0xE0u, 0x29u);
    reg_write(0xE1u, 0x03u);
    reg_write(0xE2u, (uint8_t)sdram_itv);
    reg_write(0xE3u, (uint8_t)(sdram_itv >> 8));
    reg_write(0xE4u, 0x01u);
    wait_sdram_ready();
    HAL_Delay(1u);

    value = reg_read(0x01u);
    value &= (uint8_t)~(LT768_BIT(4) | LT768_BIT(3));
    reg_write(0x01u, value);

    value = reg_read(0x02u);
    value &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(2) | LT768_BIT(1));
    reg_write(0x02u, value);

    graphic_mode();
    reg_set_bits(0x03u, 0u, LT768_BIT(1) | LT768_BIT(0));

    value = reg_read(0x12u);
    value |= LT768_BIT(7);
    value &= (uint8_t)~(LT768_BIT(4) | LT768_BIT(3) | LT768_BIT(2) | LT768_BIT(1) | LT768_BIT(0));
    reg_write(0x12u, value);

    reg_write(0x14u, (uint8_t)((LT768_SCREEN_WIDTH / 8u) - 1u));
    reg_write(0x15u, (uint8_t)(LT768_SCREEN_WIDTH % 8u));
    reg_write(0x1Au, (uint8_t)(LT768_SCREEN_HEIGHT - 1u));
    reg_write(0x1Bu, (uint8_t)((LT768_SCREEN_HEIGHT - 1u) >> 8));
    reg_write(0x16u, (uint8_t)((LT768_LCD_HBPD / 8u) - 1u));
    reg_write(0x17u, (uint8_t)(LT768_LCD_HBPD % 8u));
    reg_write(0x18u, (uint8_t)((LT768_LCD_HFPD / 8u) - 1u));
    reg_write(0x19u, (uint8_t)((LT768_LCD_HSPW < 8u) ? 0u : ((LT768_LCD_HSPW / 8u) - 1u)));
    reg_write(0x1Cu, (uint8_t)(LT768_LCD_VBPD - 1u));
    reg_write(0x1Du, 0x00u);
    reg_write(0x1Eu, (uint8_t)(LT768_LCD_VFPD - 1u));
    reg_write(0x1Fu, (uint8_t)(LT768_LCD_VSPW - 1u));

    value = reg_read(0x5Eu);
    value &= (uint8_t)~LT768_BIT(2);
    value |= LT768_BIT(1) | LT768_BIT(0);
    reg_write(0x5Eu, value);

    select_main_window_24bpp();

    value = reg_read(0x12u);
    value |= LT768_BIT(6);
    reg_write(0x12u, value);
}

static void reg_write_u16(uint8_t reg_l, uint16_t value)
{
    reg_write(reg_l, (uint8_t)value);
    reg_write((uint8_t)(reg_l + 1u), (uint8_t)(value >> 8));
}

static void reg_write_u32(uint8_t reg_l, uint32_t value)
{
    reg_write(reg_l, (uint8_t)value);
    reg_write((uint8_t)(reg_l + 1u), (uint8_t)(value >> 8));
    reg_write((uint8_t)(reg_l + 2u), (uint8_t)(value >> 16));
    reg_write((uint8_t)(reg_l + 3u), (uint8_t)(value >> 24));
}

static void framebuffer_init(void)
{
    const uint32_t frame_addr = 0u;

    reg_write_u32(0x20u, frame_addr);
    reg_write_u16(0x24u, LT768_SCREEN_WIDTH);
    reg_write_u16(0x26u, 0u);
    reg_write_u16(0x28u, 0u);

    reg_write_u32(0x50u, frame_addr);
    reg_write_u16(0x54u, LT768_SCREEN_WIDTH);
    reg_write_u16(0x56u, 0u);
    reg_write_u16(0x58u, 0u);
    reg_write_u16(0x5Au, LT768_SCREEN_WIDTH);
    reg_write_u16(0x5Cu, LT768_SCREEN_HEIGHT);
}

static void backlight_on(void)
{
    uint8_t value;

    reg_write(0x84u, 49u);

    value = reg_read(0x85u);
    value &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(2));
    value |= LT768_BIT(3);
    reg_write(0x85u, value);

    reg_write(0x8Cu, 100u);
    reg_write(0x8Du, 0u);
    reg_write(0x8Eu, 100u);
    reg_write(0x8Fu, 0u);

    value = reg_read(0x86u);
    value |= LT768_BIT(4);
    reg_write(0x86u, value);
}

static void set_foreground_color(uint32_t color)
{
    reg_write(0xD2u, (uint8_t)(color >> 16));
    reg_write(0xD3u, (uint8_t)(color >> 8));
    reg_write(0xD4u, (uint8_t)color);
}

static void set_background_color(uint32_t color)
{
    reg_write(0xD5u, (uint8_t)(color >> 16));
    reg_write(0xD6u, (uint8_t)(color >> 8));
    reg_write(0xD7u, (uint8_t)color);
}

static void select_internal_font_16(void)
{
    uint8_t value = reg_read(0xCCu);
    value &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(5) | LT768_BIT(4));
    reg_write(0xCCu, value);

    value = reg_read(0xCDu);
    value &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(3) |
                        LT768_BIT(2) | LT768_BIT(1) | LT768_BIT(0));
    reg_write(0xCDu, value);
}

static void goto_text_xy(uint16_t x, uint16_t y)
{
    reg_write(0x63u, (uint8_t)x);
    reg_write(0x64u, (uint8_t)(x >> 8));
    reg_write(0x65u, (uint8_t)y);
    reg_write(0x66u, (uint8_t)(y >> 8));
}

static void square_start_xy(uint16_t x, uint16_t y)
{
    reg_write(0x68u, (uint8_t)x);
    reg_write(0x69u, (uint8_t)(x >> 8));
    reg_write(0x6Au, (uint8_t)y);
    reg_write(0x6Bu, (uint8_t)(y >> 8));
}

static void square_end_xy(uint16_t x, uint16_t y)
{
    reg_write(0x6Cu, (uint8_t)x);
    reg_write(0x6Du, (uint8_t)(x >> 8));
    reg_write(0x6Eu, (uint8_t)y);
    reg_write(0x6Fu, (uint8_t)(y >> 8));
}

void LT768_BasicInit(void)
{
    (void)LT768_PortInit();
    s_lt768_ready = 1u;
    panel_init_1024x600();
    framebuffer_init();
    backlight_on();
    select_internal_font_16();
    graphic_mode();
}

void LT768_EnableColorBarTest(void)
{
    if (s_lt768_ready == 0u) {
        return;
    }

    reg_set_bits(0x12u, LT768_BIT(5), 0u);
}

void LT768_ShowFramebufferTest(void)
{
    if (s_lt768_ready == 0u) {
        return;
    }

    reg_set_bits(0x12u, 0u, LT768_BIT(5));
    LT768_FillRect(0u, 0u, 170u, LT768_SCREEN_HEIGHT, LT768_COLOR_RED);
    LT768_FillRect(170u, 0u, 340u, LT768_SCREEN_HEIGHT, LT768_COLOR_GREEN);
    LT768_FillRect(340u, 0u, 510u, LT768_SCREEN_HEIGHT, LT768_COLOR_BLUE);
    LT768_FillRect(510u, 0u, 680u, LT768_SCREEN_HEIGHT, LT768_COLOR_YELLOW);
    LT768_FillRect(680u, 0u, 852u, LT768_SCREEN_HEIGHT, LT768_COLOR_LIGHT_GRAY);
    LT768_FillRect(852u, 0u, LT768_SCREEN_WIDTH, LT768_SCREEN_HEIGHT, LT768_COLOR_DARK_GRAY);
}

void LT768_CaptureDiagnostics(void)
{
    static const uint8_t regs[] = {
        0x01u, 0x02u, 0x03u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u,
        0x0Au, 0x10u, 0x12u, 0x20u, 0x21u, 0x22u, 0x23u, 0x24u,
        0x50u, 0x51u, 0x52u, 0x53u, 0x54u, 0x55u, 0x5Eu, 0x84u,
        0x85u, 0x86u, 0xE0u, 0xE1u, 0xE2u
    };

    g_lt768_diagnostics[0] = 0xA5u;
    g_lt768_diagnostics[1] = LT768_ReadStatus();
    g_lt768_diagnostics[2] = (uint8_t)sizeof(regs);

    for (uint8_t i = 0u; i < (uint8_t)sizeof(regs); ++i) {
        g_lt768_diagnostics[3u + i] = reg_read(regs[i]);
    }
}

void LT768_ShowBootText(const char *text)
{
    LT768_Clear(LT768_COLOR_BLACK);
    LT768_DrawText(24u, 20u, LT768_COLOR_WHITE, LT768_COLOR_BLACK, text);
}

void LT768_Clear(uint32_t color)
{
    LT768_FillRect(0u, 0u, (uint16_t)(LT768_SCREEN_WIDTH - 1u), (uint16_t)(LT768_SCREEN_HEIGHT - 1u), color);
}

void LT768_DrawText(uint16_t x, uint16_t y, uint32_t font_color, uint32_t background_color, const char *text)
{
    if (s_lt768_ready == 0u || text == 0) {
        return;
    }

    text_mode();
    select_internal_font_16();
    set_foreground_color(font_color);
    set_background_color(background_color);
    goto_text_xy(x, y);
    LT768_WriteCommand(0x04u);
    while (*text != '\0') {
        LT768_WriteData((uint8_t)*text);
        wait_mem_write_fifo_not_full();
        ++text;
    }
    wait_2d_idle();
    graphic_mode();
}

void LT768_FillRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    if (s_lt768_ready == 0u) {
        return;
    }

    graphic_mode();
    set_foreground_color(color);
    square_start_xy(x1, y1);
    square_end_xy(x2, y2);
    reg_write(0x76u, 0xE0u);
    wait_2d_idle();
}
