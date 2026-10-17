#include "gd32f30x.h"
#include <string.h>

/* NODE_B: PB5 enables OLED2 power (low = on); PB6/PB7 are I2C SCL/SDA. */
#define OLED_PWR_PIN GPIO_PIN_5
#define OLED_SCL_PIN GPIO_PIN_6
#define OLED_SDA_PIN GPIO_PIN_7
#define OLED_I2C_WRITE_ADDRESS 0x78U /* 7-bit address 0x3C + write bit */

/* LED1 means the OLED acknowledged; LED4 means an I2C failure. */
#define LED_OK_PIN GPIO_PIN_0
#define LED_ERROR_PIN GPIO_PIN_3
#define KEY_JUMP_PIN GPIO_PIN_4  /* BSW4 / PB4, freed from JTAG by SWD remap. */
#define KEY_PAUSE_PIN GPIO_PIN_15 /* BSW1 / PC15. */
#define KEY_DUCK_PIN GPIO_PIN_14
#define KEY_RESTART_PIN GPIO_PIN_13

static uint8_t framebuffer[512];
static uint8_t displayed_framebuffer[512];
static uint8_t display_shadow_valid;
static int16_t obstacle_x;
static int16_t coin_x;
/* Jump position and velocity use half-pixel units for longer hang time. */
static int16_t jump_y;
static int16_t jump_velocity;
static uint16_t score;
static uint16_t obstacles_cleared;
static uint8_t coin_active;
static uint8_t coin_multiplier;
static uint8_t game_over;
static uint8_t game_paused;
static uint8_t duck_pose;
static uint8_t last_jump;
static uint8_t last_restart;
static uint8_t last_pause;
static uint8_t pause_cooldown;

static void delay_ms(uint32_t milliseconds)
{
    while (milliseconds-- != 0U) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U) {
        }
    }
}

static void i2c_pause(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 4U; ++i) {
        __NOP();
    }
}

static void i2c_start(void)
{
    gpio_bit_set(GPIOB, OLED_SDA_PIN | OLED_SCL_PIN);
    i2c_pause();
    gpio_bit_reset(GPIOB, OLED_SDA_PIN);
    i2c_pause();
    gpio_bit_reset(GPIOB, OLED_SCL_PIN);
}

static void i2c_stop(void)
{
    gpio_bit_reset(GPIOB, OLED_SDA_PIN);
    i2c_pause();
    gpio_bit_set(GPIOB, OLED_SCL_PIN);
    i2c_pause();
    gpio_bit_set(GPIOB, OLED_SDA_PIN);
    i2c_pause();
}

static uint8_t i2c_write_byte(uint8_t value)
{
    uint8_t bit;
    uint8_t acknowledged;

    for (bit = 0U; bit < 8U; ++bit) {
        if ((value & 0x80U) != 0U) {
            gpio_bit_set(GPIOB, OLED_SDA_PIN);
        } else {
            gpio_bit_reset(GPIOB, OLED_SDA_PIN);
        }
        i2c_pause();
        gpio_bit_set(GPIOB, OLED_SCL_PIN);
        i2c_pause();
        gpio_bit_reset(GPIOB, OLED_SCL_PIN);
        value <<= 1;
    }

    gpio_bit_set(GPIOB, OLED_SDA_PIN); /* Release SDA for the ACK bit. */
    i2c_pause();
    gpio_bit_set(GPIOB, OLED_SCL_PIN);
    i2c_pause();
    acknowledged = (gpio_input_bit_get(GPIOB, OLED_SDA_PIN) == RESET) ? 1U : 0U;
    gpio_bit_reset(GPIOB, OLED_SCL_PIN);
    i2c_pause();
    return acknowledged;
}

static uint8_t oled_command(uint8_t command)
{
    uint8_t ok;
    i2c_start();
    ok = i2c_write_byte(OLED_I2C_WRITE_ADDRESS);
    if (ok != 0U) {
        ok = i2c_write_byte(0x00U); /* SSD1306 command control byte. */
    }
    if (ok != 0U) {
        ok = i2c_write_byte(command);
    }
    i2c_stop();
    return ok;
}

static uint8_t oled_init(void)
{
    static const uint8_t commands[] = {
        0xAEU,             /* Display off while configuring. */
        0xD5U, 0x80U,      /* Display clock. */
        0xA8U, 0x1FU,      /* 32 display rows. */
        0xD3U, 0x00U,      /* No vertical offset. */
        0x40U,             /* Start at row zero. */
        0x8DU, 0x14U,      /* Internal charge pump on. */
        0xA1U, 0xC8U,      /* Segment and COM scan direction. */
        0xDAU, 0x00U,      /* COM pin configuration from panel datasheet. */
        0x81U, 0x3FU,      /* Moderate contrast for the all-pixels test. */
        0xD9U, 0x1FU,      /* Precharge period. */
        0xDBU, 0x40U,      /* VCOMH level. */
        0x20U, 0x00U,      /* Horizontal memory addressing mode. */
        0xA6U,             /* Normal (not inverted) display. */
        0xA4U,             /* Show the pixels stored in display RAM. */
        0xAFU              /* Display on. */
    };
    uint32_t i;

    for (i = 0U; i < sizeof(commands); ++i) {
        if (oled_command(commands[i]) == 0U) {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t oled_write_region(uint8_t page, uint8_t first, uint8_t last)
{
    static const uint8_t window_prefix[] = {0x21U, 0x00U, 0x00U, 0x22U, 0x00U, 0x00U};
    uint8_t window[sizeof(window_prefix)];
    uint16_t i;
    uint8_t ok;

    memcpy(window, window_prefix, sizeof(window));
    window[1] = first;
    window[2] = last;
    window[4] = page;
    window[5] = page;
    i2c_start();
    ok = i2c_write_byte(OLED_I2C_WRITE_ADDRESS);
    if (ok != 0U) {
        ok = i2c_write_byte(0x00U);
    }
    for (i = 0U; (i < sizeof(window)) && (ok != 0U); ++i) {
        ok = i2c_write_byte(window[i]);
    }
    i2c_stop();
    if (ok == 0U) {
        return 0U;
    }

    i2c_start();
    ok = i2c_write_byte(OLED_I2C_WRITE_ADDRESS);
    if (ok != 0U) {
        ok = i2c_write_byte(0x40U); /* Following bytes are image data. */
    }
    for (i = first; (i <= last) && (ok != 0U); ++i) {
        ok = i2c_write_byte(framebuffer[(uint16_t)page * 128U + i]);
    }
    i2c_stop();
    return ok;
}

static uint8_t oled_show_image(void)
{
    uint8_t page;
    uint16_t col, first, last, probe;
    for (page = 0U; page < 4U; ++page) {
        if (display_shadow_valid == 0U) {
            if (oled_write_region(page, 0U, 127U) == 0U) {
                return 0U;
            }
            memcpy(&displayed_framebuffer[(uint16_t)page * 128U],
                   &framebuffer[(uint16_t)page * 128U], 128U);
            continue;
        }
        col = 0U;
        while (col < 128U) {
            while ((col < 128U) &&
                   (framebuffer[(uint16_t)page * 128U + col] ==
                    displayed_framebuffer[(uint16_t)page * 128U + col])) {
                ++col;
            }
            if (col == 128U) {
                break;
            }
            first = col;
            last = col;
            for (probe = col + 1U; probe < 128U; ++probe) {
                if (framebuffer[(uint16_t)page * 128U + probe] !=
                    displayed_framebuffer[(uint16_t)page * 128U + probe]) {
                    last = probe;
                } else if (probe - last > 8U) {
                    break;
                }
            }
            if (oled_write_region(page, (uint8_t)first, (uint8_t)last) == 0U) {
                display_shadow_valid = 0U;
                return 0U;
            }
            memcpy(&displayed_framebuffer[(uint16_t)page * 128U + first],
                   &framebuffer[(uint16_t)page * 128U + first], last - first + 1U);
            col = last + 1U;
        }
    }
    display_shadow_valid = 1U;
    return 1U;
}

static void pixel(int16_t x, int16_t y)
{
    if ((x >= 0) && (x < 128) && (y >= 0) && (y < 32)) {
        framebuffer[(uint16_t)(y / 8) * 128U + (uint16_t)x] |= (uint8_t)(1U << (y & 7));
    }
}

static void rectangle(int16_t x, int16_t y, uint8_t width, uint8_t height)
{
    uint8_t dx, dy;
    for (dy = 0U; dy < height; ++dy) {
        for (dx = 0U; dx < width; ++dx) {
            pixel(x + dx, y + dy);
        }
    }
}

static void draw_digit(uint8_t digit, int16_t x)
{
    static const uint8_t glyphs[10][3] = {
        {0x1FU,0x11U,0x1FU}, {0x00U,0x1FU,0x00U},
        {0x1DU,0x15U,0x17U}, {0x15U,0x15U,0x1FU},
        {0x07U,0x04U,0x1FU}, {0x17U,0x15U,0x1DU},
        {0x1FU,0x15U,0x1DU}, {0x01U,0x01U,0x1FU},
        {0x1FU,0x15U,0x1FU}, {0x17U,0x15U,0x1FU}
    };
    uint8_t col, row;
    for (col = 0U; col < 3U; ++col) {
        for (row = 0U; row < 5U; ++row) {
            if ((glyphs[digit][col] & (1U << row)) != 0U) {
                pixel(x + col, 2 + row);
            }
        }
    }
}

static void draw_game(void)
{
    uint16_t number = score;
    uint8_t n;
    int16_t top = 18 - jump_y / 2;
    memset(framebuffer, 0, sizeof(framebuffer));
    rectangle(0, 29, 128U, 1U);
    if ((duck_pose != 0U) && (jump_y == 0)) {
        rectangle(12, 24, 13U, 5U);
        pixel(22, 25);
    } else {
        rectangle(13, top + 3, 7U, 8U);
        rectangle(17, top, 8U, 5U);
        rectangle(10, top + 8, 4U, 2U);
        rectangle(14, top + 11, 2U, 2U);
        rectangle(19, top + 11, 2U, 2U);
        pixel(22, top + 1);
    }
    if ((obstacles_cleared & 1U) == 0U) {
        rectangle(obstacle_x, 20, 5U, 9U);
        rectangle(obstacle_x - 2, 22, 2U, 4U);
    } else {
        rectangle(obstacle_x, 18, 8U, 3U); /* Low bird: duck under it. */
        pixel(obstacle_x + 2, 17);
        pixel(obstacle_x + 5, 16);
    }
    if (coin_active != 0U) {
        /* Five-pixel diamond at y=12..16. */
        pixel(coin_x + 2, 12);
        pixel(coin_x + 1, 13);
        pixel(coin_x + 3, 13);
        pixel(coin_x, 14);
        pixel(coin_x + 4, 14);
        pixel(coin_x + 1, 15);
        pixel(coin_x + 3, 15);
        pixel(coin_x + 2, 16);
    }
    pixel(1, 2);
    pixel(3, 4);
    pixel(1, 4);
    pixel(3, 2);
    draw_digit(coin_multiplier, 5);
    for (n = 0U; n < 4U; ++n) {
        draw_digit((uint8_t)(number % 10U), 121 - (int16_t)n * 5);
        number /= 10U;
    }
    if (game_over != 0U) {
        for (n = 0U; n < 8U; ++n) {
            pixel(60 + n, 10 + n);
            pixel(67 - n, 10 + n);
        }
    }
    if (game_paused != 0U) {
        rectangle(59, 10, 3U, 12U);
        rectangle(66, 10, 3U, 12U);
    }
}

static void reset_game(void)
{
    obstacle_x = 127;
    coin_x = 95;
    jump_y = 0;
    jump_velocity = 0;
    score = 0U;
    obstacles_cleared = 0U;
    coin_active = 1U;
    coin_multiplier = 1U;
    game_over = 0U;
    game_paused = 0U;
    duck_pose = 0U;
}

static uint8_t pressed(uint32_t port, uint32_t pin)
{
    return (gpio_input_bit_get(port, pin) == RESET) ? 1U : 0U;
}

static void update_game(uint8_t jump, uint8_t duck, uint8_t restart, uint8_t pause)
{
    uint8_t jump_edge = (jump != 0U) && (last_jump == 0U);
    uint8_t restart_edge = (restart != 0U) && (last_restart == 0U);
    uint8_t pause_edge = (pause != 0U) && (last_pause == 0U);
    uint16_t speed;
    last_jump = jump;
    last_restart = restart;
    last_pause = pause;
    if (restart_edge != 0U) {
        reset_game();
        return;
    }
    if (pause_cooldown != 0U) {
        --pause_cooldown;
    } else if ((pause_edge != 0U) && (game_over == 0U)) {
        game_paused ^= 1U;
        pause_cooldown = 4U;
    }
    if (game_paused != 0U) {
        return;
    }
    if (game_over != 0U) {
        return;
    }
    duck_pose = duck;
    if ((jump_edge != 0U) && (jump_y == 0) && (duck == 0U)) {
        jump_velocity = 11;
    }
    /* Ducking in mid-air cancels the ascent and starts a fast drop. */
    if ((duck != 0U) && (jump_y > 0) && (jump_velocity > -10)) {
        jump_velocity = -10;
    }
    if ((jump_y > 0) || (jump_velocity > 0)) {
        int16_t next = (int16_t)jump_y + jump_velocity;
        jump_velocity -= 2;
        if (next <= 0) {
            jump_y = 0;
            jump_velocity = 0;
        } else {
            jump_y = next;
        }
    }
    speed = 3U + obstacles_cleared / 4U;
    if (speed > 7U) {
        speed = 7U;
    }
    obstacle_x -= speed;
    coin_x -= speed;
    if (obstacle_x < -8) {
        obstacle_x = 127;
        coin_x = 95;
        coin_active = 1U;
        ++obstacles_cleared;
    }
    if ((obstacle_x < 25) && (obstacle_x + 8 > 12)) {
        if ((obstacles_cleared & 1U) == 0U) {
            if (jump_y < 18) {
                game_over = 1U;
            }
        } else if (!((duck != 0U) && (jump_y == 0)) && (jump_y < 26)) {
            game_over = 1U;
        }
    }
    if ((coin_active != 0U) && (game_over == 0U)) {
        if ((coin_x < 25) && (coin_x + 5 > 12) && (jump_y >= 6)) {
            score += coin_multiplier;
            coin_active = 0U;
            if (coin_multiplier < 8U) {
                coin_multiplier <<= 1;
            }
        } else if (coin_x < 8) {
            coin_active = 0U;
            coin_multiplier = 1U;
        }
    }
}

int main(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

    /* LEDs on this board are active low; start with both off. */
    gpio_bit_set(GPIOB, LED_OK_PIN | LED_ERROR_PIN);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              LED_OK_PIN | LED_ERROR_PIN);

    /* Power gate Q2 is pulled high (off) until PB5 drives it low. */
    gpio_bit_reset(GPIOB, OLED_PWR_PIN);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, OLED_PWR_PIN);

    /* The board already has I2C pull-ups. Released open-drain pins go high. */
    gpio_bit_set(GPIOB, OLED_SCL_PIN | OLED_SDA_PIN);
    gpio_init(GPIOB, GPIO_MODE_OUT_OD, GPIO_OSPEED_2MHZ,
              OLED_SCL_PIN | OLED_SDA_PIN);

    /* BSW4=PB4 jump; BSW1=PC15 pause; BSW2=PC14 duck; BSW3=PC13 restart. */
    gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              KEY_JUMP_PIN);
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              KEY_PAUSE_PIN | KEY_DUCK_PIN | KEY_RESTART_PIN);

    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    delay_ms(120U); /* Wait for switched power and the OLED reset RC network. */

    reset_game();
    draw_game();
    if ((oled_init() != 0U) && (oled_show_image() != 0U)) {
        gpio_bit_reset(GPIOB, LED_OK_PIN);
    } else {
        gpio_bit_reset(GPIOB, LED_ERROR_PIN);
        for (;;) {
            __NOP();
        }
    }

    for (;;) {
        uint8_t jump = pressed(GPIOB, KEY_JUMP_PIN);
        uint8_t duck = pressed(GPIOC, KEY_DUCK_PIN);
        uint8_t restart = pressed(GPIOC, KEY_RESTART_PIN);
        uint8_t pause = pressed(GPIOC, KEY_PAUSE_PIN);
        update_game(jump, duck, restart, pause);
        draw_game();
        if (oled_show_image() == 0U) {
            gpio_bit_reset(GPIOB, LED_ERROR_PIN);
        }
        delay_ms(20U); /* Target a stable game tick after the shorter OLED updates. */
    }
}
