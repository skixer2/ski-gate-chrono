/**
 * @file    led.h
 * @brief   LED driver for Nicla Sense ME.
 *          pin=0: onboard I2C RGB (Nicla_System)
 *          pin>0: external SK6812 strip (Adafruit_NeoPixel, needs -DHAS_NEOPIXEL)
 */

#pragma once

#include <stdint.h>

/* --- Patterns --- */
enum class LedPattern : uint8_t
{
    OFF                   = 0,
    BLUE_SLOW_FLOW        = 1,
    GREEN_CHASE           = 2,
    RED_CHASE             = 3,
    BLUE_SLOW_FLOW_POST   = 4,
    RED_FLASH_3           = 5,
};

class LED
{
public:
    LED(uint8_t pin = 0, uint8_t count = 5);

    void begin();
    void set_pattern(LedPattern p);
    void update();

private:
    /* --- onboard helpers --- */
    void begin_onboard();
    void show_onboard_breathing();
    void show_onboard_blink(uint8_t r, uint8_t g, uint8_t b);
    void show_onboard_flash3();

#ifdef HAS_NEOPIXEL
    /* --- strip helpers --- */
    void begin_strip();
    void show_strip_blue_flow();
    void show_strip_chase(uint8_t r, uint8_t g, uint8_t b);
    void show_strip_flash3();
    void set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b);
    void strip_show();
    void strip_clear();
#endif

    uint8_t     m_pin;
    uint8_t     m_count;
    bool        m_is_strip;

    LedPattern  m_pattern;
    uint32_t    m_last_ms;
    uint8_t     m_step;
    uint8_t     m_substep;
    bool        m_loop_done;
    int         m_brightness;
    int         m_dir;

#ifdef HAS_NEOPIXEL
    void*       m_strip_obj;
#endif
};
