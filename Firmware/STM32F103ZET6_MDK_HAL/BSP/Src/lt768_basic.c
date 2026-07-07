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
volatile uint8_t g_lt768_diagnostics[64];
volatile uint32_t g_lt768_progress[8];

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

static uint8_t lt768_register_signature_ok(void)
{
    const uint8_t reg01 = reg_read(0x01u);
    const uint8_t reg12 = reg_read(0x12u);
    const uint8_t reg13 = reg_read(0x13u);
    const uint8_t reg14 = reg_read(0x14u);
    const uint8_t reg15 = reg_read(0x15u);
    const uint8_t reg1A = reg_read(0x1Au);
    const uint8_t reg1B = reg_read(0x1Bu);
    g_lt768_progress[4] = ((uint32_t)reg13 << 24) |
                          ((uint32_t)reg12 << 16) |
                          ((uint32_t)reg01 << 8) |
                          LT768_ReadStatus();
    g_lt768_progress[5] = ((uint32_t)reg15 << 24) |
                          ((uint32_t)reg14 << 16) |
                          ((uint32_t)reg1B << 8) |
                          reg1A;
    g_lt768_progress[6] = ((uint32_t)reg_read(0x8Eu) << 24) |
                          ((uint32_t)reg_read(0x8Cu) << 16) |
                          ((uint32_t)reg_read(0x84u) << 8) |
                          0xA5u;

    if ((reg01 & LT768_BIT(7)) == 0u) {
        return 0u;
    }
    if ((reg12 & LT768_BIT(6)) == 0u) {
        return 0u;
    }
    if (reg13 != 0x00u) {
        return 0u;
    }
    if (reg14 != (uint8_t)((LT768_SCREEN_WIDTH / 8u) - 1u) ||
        reg15 != (uint8_t)(LT768_SCREEN_WIDTH % 8u)) {
        return 0u;
    }
    if (reg1A != (uint8_t)(LT768_SCREEN_HEIGHT - 1u) ||
        reg1B != (uint8_t)((LT768_SCREEN_HEIGHT - 1u) >> 8)) {
        return 0u;
    }
    return 1u;
}

static void reg_set_bits(uint8_t reg, uint8_t set_mask, uint8_t clear_mask)
{
    uint8_t value = reg_read(reg);
    value &= (uint8_t)~clear_mask;
    value |= set_mask;
    reg_write(reg, value);
}

static void reg_write_masked(uint8_t reg, uint8_t value, uint8_t writable_mask)
{
    uint8_t current = reg_read(reg);
    current &= (uint8_t)~writable_mask;
    current |= (uint8_t)(value & writable_mask);
    reg_write(reg, current);
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

static void wait_power_normal(void)
{
    uint32_t start = HAL_GetTick();
    while ((LT768_ReadStatus() & 0x02u) != 0u) {
        if ((HAL_GetTick() - start) > 500u) {
            break;
        }
    }
}

static void wait_initial_display_test_done(void)
{
    uint32_t start = HAL_GetTick();
    while ((LT768_ReadStatus() & 0x02u) != 0u) {
        if ((HAL_GetTick() - start) > 500u) {
            break;
        }
    }
}

static void ensure_pll_ready(void)
{
    uint8_t retries = 0u;

    while (retries < 5u) {
        wait_power_normal();
        HAL_Delay(1u);
        if ((reg_read(0x01u) & LT768_BIT(7)) != 0u) {
            return;
        }

        reg_set_bits(0x01u, LT768_BIT(7), 0u);
        HAL_Delay(1u);
        ++retries;
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
    reg_write_masked(0x03u, 0x00u, LT768_BIT(2));
}

static void text_mode(void)
{
    reg_write_masked(0x03u, LT768_BIT(2), LT768_BIT(2));
}

static void select_main_window_24bpp(void)
{
    reg_write_masked(0x10u, LT768_BIT(3), LT768_BIT(3));
}

static void display_on(void)
{
    reg_set_bits(0x12u, LT768_BIT(6), 0u);
}

static void configure_lcd_polarity(void)
{
    /* Match the vendor init: HSYNC low, VSYNC low, DE high. */
    reg_write(0x13u, 0x00u);
}

static void set_display_output_mode(uint8_t pclk_falling,
                                    uint8_t hsync_active_high,
                                    uint8_t vsync_active_high,
                                    uint8_t de_active_low,
                                    uint8_t rgb_order)
{
    uint8_t reg12 = reg_read(0x12u);
    uint8_t reg13 = reg_read(0x13u);

    /* Keep display enable / color bar / scan direction untouched. */
    reg12 &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(2) | LT768_BIT(1) | LT768_BIT(0));
    if (pclk_falling != 0u) {
        reg12 |= LT768_BIT(7);
    }
    reg12 |= (uint8_t)(rgb_order & 0x07u);

    reg13 &= (uint8_t)~(LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(5));
    if (hsync_active_high != 0u) {
        reg13 |= LT768_BIT(7);
    }
    if (vsync_active_high != 0u) {
        reg13 |= LT768_BIT(6);
    }
    if (de_active_low != 0u) {
        reg13 |= LT768_BIT(5);
    }

    reg_write(0x12u, reg12);
    reg_write(0x13u, reg13);
}

static void panel_init_1024x600(void)
{
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

    reg_write(0x01u, 0x80u);
    reg_write(0x02u, 0x00u);
    reg_write(0x03u, 0x00u);
    reg_write(0x12u, 0xC0u);

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

    reg_write(0x5Eu, 0x03u);
    reg_write(0x10u, 0x08u);
    reg_write(0x12u, 0xC0u);
    configure_lcd_polarity();
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
    /* Match the vendor LT768_PWM1_Init(1, 0, 50, 100, 100) sequence. */
    reg_write(0x84u, 49u);

    /* XPWM1 = PWM1 output, PWM1 clock divided by 1. */
    reg_set_bits(0x85u, LT768_BIT(3), LT768_BIT(7) | LT768_BIT(6) | LT768_BIT(2));

    reg_write_u16(0x8Eu, 100u);
    reg_write_u16(0x8Cu, 100u);

    /* Start PWM1 without disturbing inverter / reload settings. */
    reg_set_bits(0x86u, LT768_BIT(4), 0u);
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
    s_lt768_ready = 0u;
    g_lt768_progress[1] = 0xB100u;
    (void)LT768_PortInit();
    g_lt768_progress[1] = 0xB101u;
    wait_power_normal();
    g_lt768_progress[1] = 0xB102u;
    ensure_pll_ready();
    g_lt768_progress[1] = 0xB103u;
    wait_initial_display_test_done();
    g_lt768_progress[1] = 0xB104u;
    panel_init_1024x600();
    g_lt768_progress[1] = 0xB105u;
    framebuffer_init();
    g_lt768_progress[1] = 0xB106u;
    display_on();
    g_lt768_progress[1] = 0xB107u;
    backlight_on();
    g_lt768_progress[1] = 0xB108u;
    select_internal_font_16();
    graphic_mode();
    g_lt768_progress[1] = 0xB109u;
    LT768_CaptureDiagnostics();
    g_lt768_progress[1] = 0xB10Au;
    if (lt768_register_signature_ok() != 0u) {
        s_lt768_ready = 1u;
        g_lt768_progress[1] = 0xB10Bu;
    } else {
        s_lt768_ready = 0u;
        g_lt768_progress[1] = 0xB1EEu;
    }
    g_lt768_progress[3] = ((uint32_t)reg_read(0x13u) << 8) | reg_read(0x12u);
}

uint8_t LT768_IsReady(void)
{
    return s_lt768_ready;
}

void LT768_EnableColorBarTest(void)
{
    g_lt768_progress[2] = 0xC100u;
    if (s_lt768_ready == 0u) {
        g_lt768_progress[2] = 0xC1EEu;
    }

    /* Enable display + color bar without clobbering scan/output polarity bits. */
    reg_set_bits(0x12u, LT768_BIT(6) | LT768_BIT(5), 0u);
    g_lt768_progress[2] = 0xC101u;
    g_lt768_progress[4] = reg_read(0x12u);
    LT768_CaptureDiagnostics();
    g_lt768_progress[2] = 0xC102u;
}

void LT768_CycleDisplayCompatibilityMode(void)
{
    static const struct {
        uint8_t pclk_falling;
        uint8_t hsync_active_high;
        uint8_t vsync_active_high;
        uint8_t de_active_low;
    } modes[] = {
        {1u, 0u, 0u, 0u}, /* Vendor default: PCLK falling, HS/VS low, DE high. */
        {0u, 0u, 0u, 0u},
        {1u, 1u, 1u, 0u},
        {0u, 1u, 1u, 0u},
        {1u, 0u, 0u, 1u},
        {0u, 0u, 0u, 1u},
        {1u, 1u, 1u, 1u},
        {0u, 1u, 1u, 1u},
    };
    static const uint8_t rgb_orders[] = {
        0u, /* RGB */
        1u, /* RBG */
        2u, /* GRB */
        3u, /* GBR */
        4u, /* BRG */
        5u, /* BGR */
    };
    static uint8_t mode_index;
    static uint8_t rgb_index;
    uint8_t combined_index;

    if (s_lt768_ready == 0u) {
        return;
    }

    set_display_output_mode(modes[mode_index].pclk_falling,
                            modes[mode_index].hsync_active_high,
                            modes[mode_index].vsync_active_high,
                            modes[mode_index].de_active_low,
                            rgb_orders[rgb_index]);
    LT768_EnableColorBarTest();
    combined_index = (uint8_t)(rgb_index * (uint8_t)(sizeof(modes) / sizeof(modes[0])) + mode_index);
    g_lt768_progress[5] = combined_index;
    g_lt768_progress[6] = rgb_orders[rgb_index];
    g_lt768_progress[7] = ((uint32_t)reg_read(0x13u) << 8) | reg_read(0x12u);

    mode_index = (uint8_t)((mode_index + 1u) % (uint8_t)(sizeof(modes) / sizeof(modes[0])));
    if (mode_index == 0u) {
        rgb_index = (uint8_t)((rgb_index + 1u) % (uint8_t)(sizeof(rgb_orders) / sizeof(rgb_orders[0])));
    }
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
        0x0Au, 0x10u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
        0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu,
        0x20u, 0x21u, 0x22u, 0x23u, 0x24u,
        0x50u, 0x51u, 0x52u, 0x53u, 0x54u, 0x55u, 0x5Eu, 0x76u,
        0x84u, 0x85u, 0x86u, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu, 0xE0u,
        0xE1u, 0xE2u, 0xE3u
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
