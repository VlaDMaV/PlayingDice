#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>

/* Display dimensions — landscape 160x80 */
#define LCD_W  160
#define LCD_H   80

/* ---- Pin mapping — adjust to your board ---- */
/* SPI0: PA5=SCK(SCL), PA7=MOSI(SDA)           */
#define LCD_CS_PORT   GPIOA
#define LCD_CS_PIN    GPIO_PIN_4
#define LCD_DC_PORT   GPIOB
#define LCD_DC_PIN    GPIO_PIN_0
#define LCD_RES_PORT  GPIOB
#define LCD_RES_PIN   GPIO_PIN_1
#define LCD_BLK_PORT  GPIOB
#define LCD_BLK_PIN   GPIO_PIN_10

/* Button pins — active LOW, internal pull-up */
#define BTN1_PORT     GPIOC
#define BTN1_PIN      GPIO_PIN_13
#define BTN2_PORT     GPIOC
#define BTN2_PIN      GPIO_PIN_14

/* RGB565 colours */
#define COLOR_BLACK   0x0000u
#define COLOR_WHITE   0xFFFFu
#define COLOR_RED     0xF800u
#define COLOR_TEAL    0x0596u   /* морской волны ~#00B2AA */
#define COLOR_BRONZE  0xCBE6u   /* бронза ~#CD7F32        */
#define COLOR_DKGRAY  0x4208u   /* тёмно-серый (кубик 2)  */
#define COLOR_BGDIE   0xEF5Du   /* кремовый фон кубика    */

/* API */
void LCD_Init(void);
void LCD_Backlight(uint8_t on);
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void LCD_FillScreen(uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void LCD_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void LCD_FillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color);
void LCD_DrawChar(uint16_t x, uint16_t y, char c,
                  uint16_t fg, uint16_t bg, uint8_t scale);
void LCD_DrawStr(uint16_t x, uint16_t y, const char *s,
                 uint16_t fg, uint16_t bg, uint8_t scale);
uint16_t LCD_StrWidth(const char *s, uint8_t scale);

#endif /* ST7735_H */
