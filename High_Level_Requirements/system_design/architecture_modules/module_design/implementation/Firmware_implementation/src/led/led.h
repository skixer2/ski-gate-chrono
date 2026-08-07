/**
 * @file    led.h
 * @brief   LED driver — onboard Nicla RGB and/or SK6812 strip.
 *
 * Modes (constructor pin/count):
 *   pin=0, count=0     → onboard I2C RGB only (stock Nicla)
 *   pin=0, count=N>0   → TIMING-ONLY SK6812 sim of N LEDs (no GPIO)
 *                        + onboard mirror so bench still has a visible blink.
 *                        Measures IRQ-mask / CPU cost of show() without hardware.
 *   pin>0, count=N>0   → real NZR bit-bang on Arduino pin (custom PCB P0.19)
 *
 * Build flags (config.h / platformio):
 *   LED_STRIP_COUNT  — 0 off, 5 design, 10 stress
 *   LED_STRIP_PIN    — 0 timing-only, >0 real pin
 */

#pragma once

#include <stdint.h>

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
    /**
     * @param pin    0 = no GPIO strip (onboard and/or timing-only)
     * @param count  0 = onboard only; >0 enables SK6812 path of that length
     */
    LED(uint8_t pin = 0, uint8_t count = 0);

    void begin();
    void set_pattern(LedPattern p);
    void update();

    bool     strip_active() const { return m_is_strip; }
    bool     timing_only()  const { return m_timing_only; }
    uint8_t  strip_count()  const { return m_is_strip ? m_count : 0; }
    uint32_t last_show_us() const { return m_last_show_us; }
    uint32_t show_count()   const { return m_show_count; }

private:
    void begin_onboard();
    void show_onboard_breathing();
    void show_onboard_blink(uint8_t r, uint8_t g, uint8_t b);
    void show_onboard_flash3();

    void begin_strip();
    void show_strip_blue_flow();
    void show_strip_chase(uint8_t r, uint8_t g, uint8_t b);
    void show_strip_flash3();
    void set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b);
    void strip_clear_buf();
    void strip_show();   /* full-chain NZR or timing-equivalent */

    uint8_t     m_pin;
    uint8_t     m_count;
    bool        m_is_strip;
    bool        m_timing_only;
    bool        m_mirror_onboard; /* true when timing-only: keep Nicla RGB visible */

    LedPattern  m_pattern;
    uint32_t    m_last_ms;
    uint8_t     m_step;
    uint8_t     m_substep;
    bool        m_loop_done;
    int         m_brightness;
    int         m_dir;

    /* GRB framebuffer, max 10 LEDs for bench/stress */
    static constexpr uint8_t kMaxLeds = 10;
    uint8_t     m_grb[kMaxLeds * 3];

    uint32_t    m_last_show_us;
    uint32_t    m_show_count;
};
