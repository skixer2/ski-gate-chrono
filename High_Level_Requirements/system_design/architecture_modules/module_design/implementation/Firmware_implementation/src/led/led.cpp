/**
 * @file    led.cpp
 * @brief   Dual-mode LED driver.
 *          Default: onboard I2C RGB via Nicla_System.
 *          Strip mode: compile with -DHAS_NEOPIXEL + Adafruit_NeoPixel in lib_deps.
 */

#include "led.h"
#include <Arduino.h>
#include <Nicla_System.h>

#ifdef HAS_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#endif

/* ------------------------------------------------------------------ */
/*  Timing constants                                                   */
/* ------------------------------------------------------------------ */
static constexpr uint32_t BREATH_STEP_MS = 20;
static constexpr uint32_t BLINK_ON_MS    = 200;
static constexpr uint32_t BLINK_OFF_MS   = 300;
static constexpr uint32_t FLASH_ON_MS    = 200;
static constexpr uint32_t FLASH_OFF_MS   = 300;
static constexpr int      BREATH_MIN     = 0;
static constexpr int      BREATH_MAX     = 120;

/* ------------------------------------------------------------------ */
/*  Constructor                                                        */
/* ------------------------------------------------------------------ */
LED::LED(uint8_t pin, uint8_t count)
    : m_pin(pin), m_count(count),
      m_is_strip(pin > 0),
      m_pattern(LedPattern::OFF),
      m_last_ms(0), m_step(0), m_substep(0),
      m_loop_done(false), m_brightness(0), m_dir(1)
#ifdef HAS_NEOPIXEL
      , m_strip_obj(nullptr)
#endif
{
#ifndef HAS_NEOPIXEL
    /* Without NeoPixel, force onboard mode. Strip support requires -DHAS_NEOPIXEL. */
    (void)m_pin; (void)m_count;
    m_is_strip = false;
#endif
}

/* ------------------------------------------------------------------ */
/*  begin                                                              */
/* ------------------------------------------------------------------ */
void LED::begin()
{
    if (m_is_strip) {
#ifdef HAS_NEOPIXEL
        begin_strip();
#endif
    } else {
        begin_onboard();
    }
}

/* ------------------------------------------------------------------ */
/*  set_pattern                                                        */
/* ------------------------------------------------------------------ */
void LED::set_pattern(LedPattern p)
{
    if (m_pattern == p) return;

    if (m_is_strip) {
#ifdef HAS_NEOPIXEL
        strip_clear();
#endif
    } else {
        nicla::leds.setColor(0, 0, 0);
    }

    m_pattern    = p;
    m_step       = 0;
    m_substep    = 0;
    m_last_ms    = 0;
    m_loop_done  = false;
    m_brightness = 0;
    m_dir        = 1;
}

/* ------------------------------------------------------------------ */
/*  update                                                             */
/* ------------------------------------------------------------------ */
void LED::update()
{
    switch (m_pattern) {
    case LedPattern::OFF: return;

    case LedPattern::BLUE_SLOW_FLOW:
    case LedPattern::BLUE_SLOW_FLOW_POST:
        if (m_is_strip) {
#ifdef HAS_NEOPIXEL
            show_strip_blue_flow();
#else
            ;
#endif
        } else {
            show_onboard_breathing();
        }
        return;

    case LedPattern::GREEN_CHASE:
        if (m_is_strip) {
#ifdef HAS_NEOPIXEL
            show_strip_chase(0, 60, 0);
#else
            ;
#endif
        } else {
            show_onboard_blink(0, 60, 0);
        }
        return;

    case LedPattern::RED_CHASE:
        if (m_is_strip) {
#ifdef HAS_NEOPIXEL
            show_strip_chase(60, 0, 0);
#else
            ;
#endif
        } else {
            show_onboard_blink(60, 0, 0);
        }
        return;

    case LedPattern::RED_FLASH_3:
        if (m_is_strip) {
#ifdef HAS_NEOPIXEL
            show_strip_flash3();
#else
            ;
#endif
        } else {
            show_onboard_flash3();
        }
        return;
    }
}

/* ================================================================== */
/*  ONBOARD RGB — I2C LED driver via Nicla_System                      */
/* ================================================================== */

void LED::begin_onboard()
{
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 0);
}

void LED::show_onboard_breathing()
{
    uint32_t now = millis();

    if (m_last_ms == 0 || now - m_last_ms >= BREATH_STEP_MS) {
        m_last_ms = now;

        m_brightness += m_dir;

        if (m_brightness >= BREATH_MAX) {
            m_brightness = BREATH_MAX;
            m_dir = -1;
        } else if (m_brightness <= BREATH_MIN) {
            m_brightness = BREATH_MIN;
            m_dir = 1;
        }

        nicla::leds.setColor(0, 0, m_brightness);
    }
}

void LED::show_onboard_blink(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t now = millis();
    uint32_t dur = (m_step == 0) ? BLINK_ON_MS : BLINK_OFF_MS;

    if (m_last_ms == 0 || now - m_last_ms >= dur) {
        m_last_ms = now;

        nicla::leds.setColor(
            (m_step == 0) ? r : 0,
            (m_step == 0) ? g : 0,
            (m_step == 0) ? b : 0
        );
        m_step = 1 - m_step;
    }
}

void LED::show_onboard_flash3()
{
    if (m_loop_done) return;

    uint32_t now = millis();
    uint32_t dur = (m_substep % 2 == 0) ? FLASH_ON_MS : FLASH_OFF_MS;

    if (m_last_ms == 0 || now - m_last_ms >= dur) {
        m_last_ms = now;
        nicla::leds.setColor((m_substep % 2 == 0) ? 60 : 0, 0, 0);

        m_substep++;
        if (m_substep >= 6) {
            m_loop_done = true;
            nicla::leds.setColor(0, 0, 0);
        }
    }
}

/* ================================================================== */
/*  SK6812 STRIP — compiled only with -DHAS_NEOPIXEL                   */
/*  When strip arrives: add adafruit/Adafruit NeoPixel@^1.12.0        */
/*  to lib_deps and -DHAS_NEOPIXEL to build_flags.                    */
/* ================================================================== */

#ifdef HAS_NEOPIXEL

static constexpr uint32_t FLOW_STEP_MS   = 120;
static constexpr uint32_t CHASE_STEP_MS  = 80;

void LED::begin_strip()
{
    auto* strip = new Adafruit_NeoPixel(m_count, m_pin, NEO_GRB + NEO_KHZ800);
    strip->begin();
    strip->clear();
    strip->show();
    m_strip_obj = strip;
}

void LED::set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (!m_strip_obj || i >= m_count) return;
    auto* strip = static_cast<Adafruit_NeoPixel*>(m_strip_obj);
    strip->setPixelColor(i, strip->Color(r, g, b));
}

void LED::strip_show()
{
    if (m_strip_obj) {
        static_cast<Adafruit_NeoPixel*>(m_strip_obj)->show();
    }
}

void LED::strip_clear()
{
    if (m_strip_obj) {
        auto* strip = static_cast<Adafruit_NeoPixel*>(m_strip_obj);
        strip->clear();
        strip->show();
    }
}

void LED::show_strip_blue_flow()
{
    uint32_t now = millis();

    if (m_last_ms == 0 || now - m_last_ms >= FLOW_STEP_MS) {
        m_last_ms = now;
        strip_clear();
        set_pixel(m_step, 0, 0, 80);

        uint8_t prev = (m_step == 0) ? m_count - 1 : m_step - 1;
        set_pixel(prev, 0, 0, 20);
        strip_show();

        m_step++;
        if (m_step >= m_count) m_step = 0;
    }
}

void LED::show_strip_chase(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t now = millis();

    if (m_last_ms == 0 || now - m_last_ms >= CHASE_STEP_MS) {
        m_last_ms = now;
        strip_clear();
        set_pixel(m_step, r, g, b);
        strip_show();

        m_step++;
        if (m_step >= m_count) m_step = 0;
    }
}

void LED::show_strip_flash3()
{
    if (m_loop_done) return;

    uint32_t now = millis();
    uint32_t dur = (m_substep % 2 == 0) ? FLASH_ON_MS : FLASH_OFF_MS;

    if (m_last_ms == 0 || now - m_last_ms >= dur) {
        m_last_ms = now;

        if (m_substep % 2 == 0) {
            for (uint8_t i = 0; i < m_count; i++) set_pixel(i, 80, 0, 0);
        } else {
            strip_clear();
        }
        strip_show();

        m_substep++;
        if (m_substep >= 6) {
            m_loop_done = true;
            strip_clear();
        }
    }
}

#endif /* HAS_NEOPIXEL */
