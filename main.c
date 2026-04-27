/*  PlayingDice — GD32F303CCT6 + 0.96" 80x160 IPS ST7735S
 *
 *  Pins (adjust in st7735.h if needed):
 *    Display SPI0 : PA5=SCK, PA7=MOSI
 *    CS  : PA4    DC : PB0    RES : PB1    BLK : PB10
 *    BTN1: PC13   BTN2: PC14   (active LOW, internal pull-up)
 */

/* GD32F303CCT6 is High-Density — must be defined before any GD header.
   IAR defines it via project preprocessor; VS Code IntelliSense may not. */
#ifndef GD32F30X_HD
#define GD32F30X_HD
#endif

#include "gd32f30x.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_pmu.h"
#include "gd32f30x_exti.h"
#include "gd32f30x_misc.h"
#include "system_gd32f30x.h"
#include "st7735.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  SysTick                                                             */
/* ------------------------------------------------------------------ */

volatile uint32_t msTicks = 0;

void SysTick_Handler(void) { msTicks++; }

void delay_ms(uint32_t ms)
{
    uint32_t t = msTicks;
    while ((msTicks - t) < ms);
}

/* ------------------------------------------------------------------ */
/*  Minimal LCG random                                                  */
/* ------------------------------------------------------------------ */

static uint32_t rng_state = 0xDEADBEEFu;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static uint8_t rng_dice(uint8_t max_val)
{
    return (uint8_t)((rng_next() >> 8) % max_val) + 1u;
}

/* ------------------------------------------------------------------ */
/*  Layout constants                                                    */
/* ------------------------------------------------------------------ */

#define BAR_H        14    /* top status bar height                    */
#define DIE_SIZE     35    /* die square side                          */
#define DIE_RADIUS    5    /* corner radius                            */
#define DOT_R         3    /* dot radius                               */

/* Die centers — fixed y, x changes only during animation              */
#define DIE_Y_C      47    /* vertical center of both dice             */
#define DIE1_X_C     40    /* final x-center of die 1 (red dots)      */
#define DIE2_X_C    120    /* final x-center of die 2 (white dots)    */

/* Start positions for fly-in animation */
#define DIE1_X_START (-20)
#define DIE2_X_START  180

/* ------------------------------------------------------------------ */
/*  Game state                                                          */
/* ------------------------------------------------------------------ */

typedef enum { S_IDLE, S_SPLASH, S_ANIM, S_RESULT } AppState;

static AppState  g_state       = S_IDLE;
static uint32_t  g_state_ms    = 0;
static uint8_t   g_max8        = 0;   /* "no more than 8" mode         */
static uint8_t   g_dice_count  = 2;   /* 1 or 2 dice                   */
static uint8_t   g_die1_val    = 1;
static uint8_t   g_die2_val    = 1;
static uint8_t   g_anim_lock   = 0;   /* 1 while animation runs        */
static uint32_t  g_idle_ms     = 0;   /* timestamp of last activity in RESULT */
static uint8_t   g_splash_auto = 0;   /* 1 = go to ANIM right after splash  */

/* Button tracking */
static uint8_t  btn1_last       = 1;
static uint8_t  btn2_last       = 1;
static uint32_t btn2_dn_ms      = 0;
static uint8_t  btn2_long_fired = 0;

/* Animation state */
static int16_t  ax1, ax2;            /* current x-centers             */
static int16_t  prev_ax1, prev_ax2;  /* previous x-centers (for erase) */
static uint8_t  af1, af2;            /* displayed faces               */
static uint32_t a_spin_ms   = 0;     /* last face-change timestamp    */
static uint8_t  a_fi1, a_fi2;        /* index into spin sequence      */

static const uint8_t k_spin_seq[6] = {1, 4, 2, 5, 3, 6};

/* ------------------------------------------------------------------ */
/*  Dot patterns per die face (dx, dy relative to die center)          */
/*  Grid spacing: ±11 px                                               */
/* ------------------------------------------------------------------ */

typedef struct { int8_t dx, dy; } Dot;

static const Dot k_dots[7][6] = {
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},         /* [0] unused      */
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},         /* 1: centre       */
    {{ 11,-11},{-11, 11},{0,0},{0,0},{0,0},{0,0}}, /* 2              */
    {{ 11,-11},{  0,  0},{-11,11},{0,0},{0,0},{0,0}}, /* 3           */
    {{ 11,-11},{-11,-11},{ 11,11},{-11,11},{0,0},{0,0}}, /* 4        */
    {{ 11,-11},{-11,-11},{  0, 0},{ 11,11},{-11,11},{0,0}}, /* 5     */
    {{ 11,-11},{-11,-11},{ 11, 0},{-11, 0},{ 11,11},{-11,11}}, /* 6 */
};

static const uint8_t k_dot_cnt[7] = {0,1,2,3,4,5,6};

/* ------------------------------------------------------------------ */
/*  Die drawing                                                         */
/* ------------------------------------------------------------------ */

static void draw_die(int16_t cx, int16_t cy, uint8_t face,
                     uint16_t body_col, uint16_t dot_col)
{
    int16_t x = cx - DIE_SIZE/2;
    int16_t y = cy - DIE_SIZE/2;

    /* Skip if completely outside screen */
    if (x + DIE_SIZE <= 0 || x >= LCD_W) return;
    if (y + DIE_SIZE <= 0 || y >= LCD_H) return;

    LCD_FillRoundRect(x, y, DIE_SIZE, DIE_SIZE, DIE_RADIUS, body_col);

    if (face < 1 || face > 6) return;

    uint8_t n = k_dot_cnt[face];
    uint8_t i;
    for (i = 0; i < n; i++) {
        LCD_FillCircle(cx + k_dots[face][i].dx,
                       cy + k_dots[face][i].dy,
                       DOT_R, dot_col);
    }
}

/* ------------------------------------------------------------------ */
/*  Bullet icon (max-8 mode indicator)                                  */
/*  Drawn at top-left, 12x14 px region                                 */
/* ------------------------------------------------------------------ */

static void draw_bullet_icon(uint8_t visible)
{
    LCD_FillRect(1, 0, 14, BAR_H, COLOR_BLACK);
    if (!visible) return;

    /* Case body */
    LCD_FillRoundRect(3, 6, 8, 7, 2, COLOR_BRONZE);
    /* Ogive (rounded tip) — 3 rows narrowing */
    LCD_FillRect(5, 3, 4, 4, COLOR_BRONZE);
    LCD_FillRect(6, 2, 2, 2, COLOR_BRONZE);
    /* Rim */
    LCD_FillRect(2, 12, 10, 2, COLOR_BRONZE);
}

/* ------------------------------------------------------------------ */
/*  Dice-count indicator — small squares, top-right                    */
/* ------------------------------------------------------------------ */

static void draw_count_icon(uint8_t count)
{
    LCD_FillRect(LCD_W - 17, 0, 17, BAR_H, COLOR_BLACK);
    if (count >= 2) {
        LCD_FillRoundRect(LCD_W - 16, 4, 6, 6, 1, COLOR_WHITE);
        LCD_FillRoundRect(LCD_W -  8, 4, 6, 6, 1, COLOR_WHITE);
    } else {
        LCD_FillRoundRect(LCD_W - 12, 4, 6, 6, 1, COLOR_WHITE);
    }
}

/* ------------------------------------------------------------------ */
/*  Status bar (call after any mode change)                             */
/* ------------------------------------------------------------------ */

static void draw_status_bar(void)
{
    LCD_FillRect(0, 0, LCD_W, BAR_H, COLOR_BLACK);
    draw_bullet_icon(g_max8);
    draw_count_icon(g_dice_count);
}

/* ------------------------------------------------------------------ */
/*  Splash screen: white bg, "by" black, "Xelor" teal, scale 3         */
/* ------------------------------------------------------------------ */

static void show_splash(void)
{
    LCD_FillScreen(COLOR_WHITE);

    /* "by Xelor" centred, 8 chars x scale 3
       width = 8*5*3 + 7*3 = 141 px  =>  x_start = (160-141)/2 = 9
       height = 7*3 = 21 px           =>  y_start = (80-21)/2 = 29   */
    uint16_t x = 9;
    uint16_t y = 29;
    uint8_t  s = 3;

    LCD_DrawChar(x,       y, 'b', COLOR_BLACK, COLOR_WHITE, s); x += 18;
    LCD_DrawChar(x,       y, 'y', COLOR_BLACK, COLOR_WHITE, s); x += 18;
    x += 18; /* space */
    LCD_DrawChar(x,       y, 'X', COLOR_TEAL,  COLOR_WHITE, s); x += 18;
    LCD_DrawChar(x,       y, 'e', COLOR_TEAL,  COLOR_WHITE, s); x += 18;
    LCD_DrawChar(x,       y, 'l', COLOR_TEAL,  COLOR_WHITE, s); x += 18;
    LCD_DrawChar(x,       y, 'o', COLOR_TEAL,  COLOR_WHITE, s); x += 18;
    LCD_DrawChar(x,       y, 'r', COLOR_TEAL,  COLOR_WHITE, s);
}

/* ------------------------------------------------------------------ */
/*  Score display (shown after animation ends)                          */
/*  Score row occupies y = BAR_H .. BAR_H+13 (14 px, scale-2 font)    */
/* ------------------------------------------------------------------ */

static void draw_score(void)
{
    uint16_t y  = BAR_H;
    uint8_t  sc = 2;
    uint16_t x;

    LCD_FillRect(0, y, LCD_W, 14, COLOR_BLACK);

    if (g_dice_count == 2) {
        /* Build string segments: d1 "+" d2 "=" sum               */
        /* Use fixed 12-px steps (scale-2 char = 10 wide + 2 gap) */
        uint8_t sum = g_die1_val + g_die2_val;
        uint8_t chars = (sum >= 10) ? 6 : 5;  /* "d+d=dd" or "d+d=d" */
        x = (uint16_t)((LCD_W - chars * 12) / 2);

        char tmp[2] = {0, 0};

        tmp[0] = '0' + g_die1_val;
        LCD_DrawStr(x, y, tmp, COLOR_RED,   COLOR_BLACK, sc); x += 12;

        LCD_DrawStr(x, y, "+", COLOR_WHITE, COLOR_BLACK, sc); x += 12;

        tmp[0] = '0' + g_die2_val;
        LCD_DrawStr(x, y, tmp, COLOR_WHITE, COLOR_BLACK, sc); x += 12;

        LCD_DrawStr(x, y, "=", COLOR_WHITE, COLOR_BLACK, sc); x += 12;

        if (sum >= 10) {
            LCD_DrawStr(x, y, "1", COLOR_TEAL, COLOR_BLACK, sc); x += 12;
            tmp[0] = '0' + (sum - 10);
        } else {
            tmp[0] = '0' + sum;
        }
        LCD_DrawStr(x, y, tmp, COLOR_TEAL, COLOR_BLACK, sc);

    } else {
        /* Single die: value centred in white */
        char tmp[2] = {'0' + g_die1_val, 0};
        x = (uint16_t)((LCD_W - 10) / 2);
        LCD_DrawStr(x, y, tmp, COLOR_WHITE, COLOR_BLACK, sc);
    }
}

/* ------------------------------------------------------------------ */
/*  Dice generation respecting mode constraints                         */
/* ------------------------------------------------------------------ */

static void generate_dice(void)
{
    rng_state ^= msTicks;   /* stir in timing entropy */

    g_die1_val = rng_dice(6);

    if (g_dice_count == 2) {
        if (g_max8) {
            uint8_t mx = 8u - g_die1_val;
            if (mx > 6) mx = 6;
            if (mx < 1) mx = 1;
            g_die2_val = rng_dice(mx);
        } else {
            g_die2_val = rng_dice(6);
        }
    }
}

/* Erase the bounding box of one die (with 1-px margin) at center cx,cy */
static void erase_die_box(int16_t cx, int16_t cy)
{
    int16_t x = cx - (DIE_SIZE / 2 + 1);
    int16_t y = cy - (DIE_SIZE / 2 + 1);
    int16_t w = DIE_SIZE + 2;
    int16_t h = DIE_SIZE + 2;

    /* Clip to content area */
    if (x < 0)        { w += x; x = 0; }
    if (y < BAR_H)    { h -= (BAR_H - y); y = BAR_H; }
    if (w <= 0 || h <= 0) return;
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;

    LCD_FillRect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, COLOR_BLACK);
}

static void anim_render(void)
{
    /* Erase ONLY the previous die bounding boxes — no full-screen clear.
       This eliminates the black flash that caused flickering.           */
    erase_die_box(prev_ax1, DIE_Y_C);
    if (g_dice_count == 2)
        erase_die_box(prev_ax2, DIE_Y_C);

    /* Draw dice at new positions */
    draw_die(ax1, DIE_Y_C, af1, COLOR_WHITE,  COLOR_RED);
    if (g_dice_count == 2)
        draw_die(ax2, DIE_Y_C, af2, COLOR_DKGRAY, COLOR_WHITE);

    /* Remember current positions for next frame's erase */
    prev_ax1 = ax1;
    prev_ax2 = ax2;
}

/* ------------------------------------------------------------------ */
/*  Animation start                                                     */
/* ------------------------------------------------------------------ */

static void start_anim(void)
{
    generate_dice();
    ax1 = DIE1_X_START;   prev_ax1 = DIE1_X_START;
    ax2 = DIE2_X_START;   prev_ax2 = DIE2_X_START;
    a_fi1 = 0; a_fi2 = 3;
    af1 = k_spin_seq[0];
    af2 = k_spin_seq[3];
    a_spin_ms   = msTicks;
    g_anim_lock = 1;

    /* One full clear to set a clean background, then draw first frame */
    LCD_FillScreen(COLOR_BLACK);
    draw_status_bar();
    draw_die(ax1, DIE_Y_C, af1, COLOR_WHITE,  COLOR_RED);
    if (g_dice_count == 2)
        draw_die(ax2, DIE_Y_C, af2, COLOR_DKGRAY, COLOR_WHITE);

    g_state    = S_ANIM;
    g_state_ms = msTicks;
}

/* ------------------------------------------------------------------ */
/*  Animation helpers                                                   */
/* ------------------------------------------------------------------ */

/* Ease-out quadratic: start→target over 0..256 normalised time */
static int16_t ease_out(int16_t start, int16_t target, uint32_t t256)
{
    uint32_t inv;
    uint32_t factor;

    if (t256 >= 256u) return target;
    inv    = 256u - t256;
    factor = (inv * inv) >> 8u;                   /* (1-t)^2 in Q8 */
    return (int16_t)((int32_t)target +
                     ((int32_t)(start - target) * (int32_t)factor >> 8));
}


/* ------------------------------------------------------------------ */
/*  Sleep / wakeup                                                      */
/* ------------------------------------------------------------------ */

static volatile uint8_t g_wakeup = 0;

/* Only BTN1 (PC13) wakes the system */
void EXTI10_15_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_13)) {
        exti_interrupt_flag_clear(EXTI_13);
        g_wakeup = 1;
    }
}

static void system_sleep(void)
{
    LCD_Backlight(0);
    LCD_FillScreen(COLOR_BLACK);

    /* EXTI on BTN1 (PC13) only — falling edge */
    rcu_periph_clock_enable(RCU_AF);
    gpio_exti_source_select(GPIO_PORT_SOURCE_GPIOC, GPIO_PIN_SOURCE_13);
    exti_interrupt_flag_clear(EXTI_13);
    exti_init(EXTI_13, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    nvic_irq_enable(EXTI10_15_IRQn, 2, 0);

    g_wakeup = 0;
    while (!g_wakeup) __WFI();

    nvic_irq_disable(EXTI10_15_IRQn);
    exti_deinit();

    /* Resume: show splash, then auto-start animation */
    LCD_Backlight(1);
    g_anim_lock    = 0;
    btn1_last      = 1;
    btn2_last      = 1;
    g_splash_auto  = 1;   /* after splash → auto-start ANIM */

    delay_ms(80);         /* debounce: wait for button to settle */
    show_splash();
    g_state    = S_SPLASH;
    g_state_ms = msTicks;
}

/* ------------------------------------------------------------------ */
/*  Button polling (called every main-loop tick ~30 ms)                 */
/* ------------------------------------------------------------------ */

static void handle_buttons(void)
{
    uint8_t btn1 = (GPIO_ISTAT(BTN1_PORT) & BTN1_PIN) ? 1u : 0u;
    uint8_t btn2 = (GPIO_ISTAT(BTN2_PORT) & BTN2_PIN) ? 1u : 0u;

    /* ---------- BTN2 long-press detection ---------- */
    if (!btn2 && btn2_last) {
        btn2_dn_ms      = msTicks;
        btn2_long_fired = 0;
    }
    if (!btn2 && !btn2_long_fired &&
        (msTicks - btn2_dn_ms) > 600u &&
        !g_anim_lock) {
        /* Long press: toggle dice count 1↔2 */
        g_dice_count    = (g_dice_count == 2u) ? 1u : 2u;
        btn2_long_fired = 1;
        draw_status_bar();
    }

    /* BTN2 short press (on release, long-press not yet fired) */
    if (btn2 && !btn2_last && !btn2_long_fired && !g_anim_lock) {
        g_max8 = g_max8 ? 0u : 1u;
        draw_status_bar();
    }

    /* ---------- BTN1: start roll (direct to animation, no splash) ---- */
    if (!btn1 && btn1_last && !g_anim_lock) {
        if (g_state == S_IDLE || g_state == S_RESULT)
            start_anim();
    }

    /* Any button resets the 20-second idle counter in RESULT */
    if (g_state == S_RESULT) {
        if ((!btn1 && btn1_last) || (!btn2 && btn2_last))
            g_idle_ms = msTicks;
    }

    btn1_last = btn1;
    btn2_last = btn2;
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    SystemInit();
    SysTick_Config(SystemCoreClock / 1000u);

    rcu_periph_clock_enable(RCU_PMU);
    LCD_Init();

    /* Boot splash — after it expires, go to IDLE (wait for BTN1) */
    draw_status_bar();
    show_splash();
    g_splash_auto = 0;
    g_state       = S_SPLASH;
    g_state_ms    = msTicks;

    while (1) {
        handle_buttons();

        switch (g_state) {

        /* ---- IDLE: black screen, mode icons, wait for BTN1 ---- */
        case S_IDLE:
            break;

        /* ---- SPLASH: "by Xelor" for 1 s ----------------------- */
        case S_SPLASH:
            if ((msTicks - g_state_ms) >= 1000u) {
                if (g_splash_auto) {
                    /* After sleep: go straight to animation */
                    g_splash_auto = 0;
                    start_anim();
                } else {
                    /* After boot: wait for BTN1 */
                    LCD_FillScreen(COLOR_BLACK);
                    draw_status_bar();
                    g_state    = S_IDLE;
                    g_state_ms = msTicks;
                }
            }
            break;

        /* ---- ANIM: dice fly in, spin, settle ------------------- */
        case S_ANIM: {
            uint32_t elapsed = msTicks - g_state_ms;

            /* Position ease-out over 3000 ms */
            uint32_t t256 = (elapsed < 3000u) ? (elapsed * 256u / 3000u) : 256u;
            ax1 = ease_out(DIE1_X_START, DIE1_X_C, t256);
            ax2 = ease_out(DIE2_X_START, DIE2_X_C, t256);

            /* Face spin — slows down, then locks */
            if (elapsed < 3200u) {
                uint32_t interval;
                if      (elapsed < 600u)  interval = 80u;
                else if (elapsed < 1500u) interval = 150u;
                else if (elapsed < 2400u) interval = 280u;
                else                      interval = 550u;

                if ((msTicks - a_spin_ms) >= interval) {
                    a_spin_ms = msTicks;
                    a_fi1 = (uint8_t)((a_fi1 + 1u) % 6u);
                    a_fi2 = (uint8_t)((a_fi2 + 1u) % 6u);
                    af1 = k_spin_seq[a_fi1];
                    af2 = k_spin_seq[a_fi2];
                }
            } else {
                /* Locked to final values */
                af1 = g_die1_val;
                af2 = g_die2_val;
            }

            anim_render();

            /* Animation complete at 3500 ms */
            if (elapsed >= 3500u) {
                g_anim_lock = 0;

                /* Dice are already drawn at correct positions by the last
                   anim_render() call — just overlay the score row.      */
                draw_score();

                g_state    = S_RESULT;
                g_state_ms = msTicks;
                g_idle_ms  = msTicks;
            }
            break;
        }

        /* ---- RESULT: show result, 20-second sleep timer -------- */
        case S_RESULT:
            if ((msTicks - g_idle_ms) >= 20000u)
                system_sleep();
            break;
        }

        /* Yield only in non-animated states; animation is SPI-limited */
        if (g_state != S_ANIM)
            delay_ms(10);
    }
}
