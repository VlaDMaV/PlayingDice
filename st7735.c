#ifndef GD32F30X_HD
#define GD32F30X_HD
#endif

#include "st7735.h"
#include "gd32f30x.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_spi.h"

/* ------------------------------------------------------------------ */
/*  Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

static inline void cs_lo(void)  { GPIO_BC(LCD_CS_PORT)  = LCD_CS_PIN;  }
static inline void cs_hi(void)  { GPIO_BOP(LCD_CS_PORT) = LCD_CS_PIN;  }
static inline void dc_lo(void)  { GPIO_BC(LCD_DC_PORT)  = LCD_DC_PIN;  }  /* cmd  */
static inline void dc_hi(void)  { GPIO_BOP(LCD_DC_PORT) = LCD_DC_PIN;  }  /* data */
static inline void res_lo(void) { GPIO_BC(LCD_RES_PORT) = LCD_RES_PIN; }
static inline void res_hi(void) { GPIO_BOP(LCD_RES_PORT)= LCD_RES_PIN; }

extern void delay_ms(uint32_t ms);   /* defined in main.c */

static void spi_byte(uint8_t b)
{
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE));
    spi_i2s_data_transmit(SPI0, b);
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE));
    (void)spi_i2s_data_receive(SPI0);
}

/* Init-sequence helpers — CS toggles per byte, fine for slow cmds */
static void lcd_cmd(uint8_t c)  { dc_lo(); cs_lo(); spi_byte(c); cs_hi(); }
static void lcd_dat(uint8_t d)  { dc_hi(); cs_lo(); spi_byte(d); cs_hi(); }

/* --- Drawing helper: CASET + RASET + RAMWR in ONE CS transaction.
       CS is driven LOW on entry and stays LOW on exit.
       Caller must raise CS after sending pixel data.            --- */
static void write_window_raw(uint16_t xs, uint16_t xe,
                             uint16_t ys, uint16_t ye)
{
    cs_lo();
    dc_lo(); spi_byte(0x2A); dc_hi();          /* CASET */
    spi_byte(xs >> 8); spi_byte(xs & 0xFF);
    spi_byte(xe >> 8); spi_byte(xe & 0xFF);
    dc_lo(); spi_byte(0x2B); dc_hi();          /* RASET */
    spi_byte(ys >> 8); spi_byte(ys & 0xFF);
    spi_byte(ye >> 8); spi_byte(ye & 0xFF);
    dc_lo(); spi_byte(0x2C); dc_hi();          /* RAMWR — DC stays HI for pixels */
}

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */

void LCD_Init(void)
{
    /* clocks */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_SPI0);

    /* PA5=SCK, PA7=MOSI */
    gpio_init(GPIOA, GPIO_MODE_AF_PP,  GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_7);
    /* PA4=CS */
    gpio_init(LCD_CS_PORT,  GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_CS_PIN);
    /* PB0=DC, PB1=RES, PB10=BLK */
    gpio_init(LCD_DC_PORT,  GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_DC_PIN);
    gpio_init(LCD_RES_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_RES_PIN);
    gpio_init(LCD_BLK_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_BLK_PIN);

    /* BTN1=PC13, BTN2=PC14 — input pull-up */
    gpio_init(BTN1_PORT, GPIO_MODE_IPU, GPIO_OSPEED_2MHZ, BTN1_PIN);
    gpio_init(BTN2_PORT, GPIO_MODE_IPU, GPIO_OSPEED_2MHZ, BTN2_PIN);

    /* SPI0: master, 8-bit, mode0, MSB first */
    spi_parameter_struct sp;
    spi_struct_para_init(&sp);
    sp.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    sp.device_mode          = SPI_MASTER;
    sp.frame_size           = SPI_FRAMESIZE_8BIT;
    sp.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    sp.nss                  = SPI_NSS_SOFT;
    sp.prescale             = SPI_PSC_4;   /* 120/4 = 30 MHz */
    sp.endian               = SPI_ENDIAN_MSB;
    spi_init(SPI0, &sp);
    spi_enable(SPI0);

    cs_hi();

    /* Hardware reset */
    res_hi(); delay_ms(5);
    res_lo(); delay_ms(20);
    res_hi(); delay_ms(150);

    /* ST7735S init sequence */
    lcd_cmd(0x01); delay_ms(150);   /* SW reset        */
    lcd_cmd(0x11); delay_ms(255);   /* Sleep out       */

    lcd_cmd(0xB1);                  /* Frame rate normal */
    lcd_dat(0x01); lcd_dat(0x2C); lcd_dat(0x2D);

    lcd_cmd(0xB2);                  /* Frame rate idle */
    lcd_dat(0x01); lcd_dat(0x2C); lcd_dat(0x2D);

    lcd_cmd(0xB3);                  /* Frame rate partial */
    lcd_dat(0x01); lcd_dat(0x2C); lcd_dat(0x2D);
    lcd_dat(0x01); lcd_dat(0x2C); lcd_dat(0x2D);

    lcd_cmd(0xB4); lcd_dat(0x07);   /* Column inversion  */

    lcd_cmd(0xC0);                  /* Power ctrl 1 */
    lcd_dat(0xA2); lcd_dat(0x02); lcd_dat(0x84);
    lcd_cmd(0xC1); lcd_dat(0xC5);
    lcd_cmd(0xC2); lcd_dat(0x0A); lcd_dat(0x00);
    lcd_cmd(0xC3); lcd_dat(0x8A); lcd_dat(0x2A);
    lcd_cmd(0xC4); lcd_dat(0x8A); lcd_dat(0xEE);
    lcd_cmd(0xC5); lcd_dat(0x0E); /* VCOM */

    lcd_cmd(0x36); lcd_dat(0x68);   /* MADCTL landscape: MX|MV|BGR */

    lcd_cmd(0x3A); lcd_dat(0x05);   /* 16-bit colour RGB565 */

    /* CASET x: 1..160  (offset 1) */
    lcd_cmd(0x2A);
    lcd_dat(0x00); lcd_dat(0x00 + LCD_X_OFFSET);
    lcd_dat(0x00); lcd_dat(0x9F + LCD_X_OFFSET);

    /* RASET y: 26..105 (offset 26) */
    lcd_cmd(0x2B);
    lcd_dat(0x00); lcd_dat(0x00 + LCD_Y_OFFSET);
    lcd_dat(0x00); lcd_dat(0x4F + LCD_Y_OFFSET);

    /* Gamma positive */
    lcd_cmd(0xE0);
    lcd_dat(0x02); lcd_dat(0x1C); lcd_dat(0x07); lcd_dat(0x12);
    lcd_dat(0x37); lcd_dat(0x32); lcd_dat(0x29); lcd_dat(0x2D);
    lcd_dat(0x29); lcd_dat(0x25); lcd_dat(0x2B); lcd_dat(0x39);
    lcd_dat(0x00); lcd_dat(0x01); lcd_dat(0x03); lcd_dat(0x10);

    /* Gamma negative */
    lcd_cmd(0xE1);
    lcd_dat(0x03); lcd_dat(0x1D); lcd_dat(0x07); lcd_dat(0x06);
    lcd_dat(0x2E); lcd_dat(0x2C); lcd_dat(0x29); lcd_dat(0x2D);
    lcd_dat(0x2E); lcd_dat(0x2E); lcd_dat(0x37); lcd_dat(0x3F);
    lcd_dat(0x00); lcd_dat(0x00); lcd_dat(0x02); lcd_dat(0x10);

    lcd_cmd(0x13); delay_ms(10);    /* Normal display  */

    /* --- Wipe entire controller RAM (162 x 132) before display-on.
           This eliminates border artifacts from uninitialised memory
           at addresses outside our CASET/RASET window.            --- */
    write_window_raw(0, 161, 0, 131);
    {
        uint32_t n = 162u * 132u;
        while (n--) { spi_byte(0); spi_byte(0); }
    }
    cs_hi();

    lcd_cmd(0x29); delay_ms(100);   /* Display on      */

    LCD_Backlight(1);
}

void LCD_Backlight(uint8_t on)
{
    if (on) GPIO_BOP(LCD_BLK_PORT) = LCD_BLK_PIN;
    else     GPIO_BC(LCD_BLK_PORT)  = LCD_BLK_PIN;
}

/* ------------------------------------------------------------------ */
/*  Drawing primitives                                                  */
/* ------------------------------------------------------------------ */

/* Public SetWindow — used externally; CS is raised on return */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    write_window_raw(x0 + LCD_X_OFFSET, x1 + LCD_X_OFFSET,
                     y0 + LCD_Y_OFFSET, y1 + LCD_Y_OFFSET);
    cs_hi();
}

/* FillRect: window setup + pixel burst in a SINGLE CS transaction */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t  hi, lo;
    uint32_t n;
    if (!w || !h) return;
    write_window_raw(x + LCD_X_OFFSET,       x + w - 1 + LCD_X_OFFSET,
                     y + LCD_Y_OFFSET,       y + h - 1 + LCD_Y_OFFSET);
    hi = color >> 8; lo = color & 0xFF;
    n  = (uint32_t)w * h;
    while (n--) { spi_byte(hi); spi_byte(lo); }
    cs_hi();
}

void LCD_FillScreen(uint16_t color) { LCD_FillRect(0, 0, LCD_W, LCD_H, color); }

void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    write_window_raw(x + LCD_X_OFFSET, x + LCD_X_OFFSET,
                     y + LCD_Y_OFFSET, y + LCD_Y_OFFSET);
    spi_byte(color >> 8); spi_byte(color & 0xFF);
    cs_hi();
}

/* Clip-safe fill: accepts int16_t, clips to screen */
static void fill_rc(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    LCD_FillRect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

static int16_t circ_dx(int16_t r, int16_t dy)
{
    int32_t rr = (int32_t)r*r - (int32_t)dy*dy;
    int16_t dx = 0;
    while ((int32_t)(dx+1)*(dx+1) <= rr) dx++;
    return dx;
}

void LCD_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = circ_dx(r, dy);
        fill_rc(cx - dx, cy + dy, 2*dx + 1, 1, color);
    }
}

void LCD_FillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color)
{
    /* horizontal center band */
    fill_rc(x, y + r, w, h - 2*r, color);
    /* top/bottom rounded caps */
    for (int16_t i = 0; i <= r; i++) {
        int16_t dx = circ_dx(r, i);
        int16_t span = 2*dx + 1;
        int16_t lx = x + r - dx;
        /* top cap row */
        fill_rc(lx, y + r - i, span + (w - 2*r) - 1, 1, color);
        /* bottom cap row */
        fill_rc(lx, y + h - 1 - r + i, span + (w - 2*r) - 1, 1, color);
    }
}

/* ------------------------------------------------------------------ */
/*  Font 5x7 (column-based, bit0=top, 95 printable ASCII chars 0x20-0x7E) */
/* ------------------------------------------------------------------ */

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x08,0x54,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x40,0x3C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

void LCD_DrawChar(uint16_t x, uint16_t y, char c,
                  uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *bmp = font5x7[(uint8_t)c - 0x20];
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = bmp[col];
        for (uint8_t row = 0; row < 7; row++) {
            uint16_t color = (bits & (1 << row)) ? fg : bg;
            LCD_FillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void LCD_DrawStr(uint16_t x, uint16_t y, const char *s,
                 uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*s) {
        LCD_DrawChar(x, y, *s++, fg, bg, scale);
        x += (5 + 1) * scale;
    }
}

uint16_t LCD_StrWidth(const char *s, uint8_t scale)
{
    uint16_t n = 0;
    while (*s++) n++;
    return n ? (n * 5 + (n - 1)) * scale : 0;
}
