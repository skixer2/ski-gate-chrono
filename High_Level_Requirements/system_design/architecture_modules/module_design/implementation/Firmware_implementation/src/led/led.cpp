/**
 * @file    led.cpp
 * @brief   Onboard I2C RGB + native SK6812 NZR (no Adafruit_NeoPixel).
 *
 * Strip show() bit-bangs WS2812/SK6812 timing at ~800 kHz.
 * With pin=0 + count>0: same IRQ-off duration and bit work, no GPIO
 * (bench cost of commanding the strip without hardware attached).
 */

#include "led.h"
#include <Arduino.h>
#include <Nicla_System.h>
#include <string.h>

#if defined(NRF_GPIO)
#include "nrf.h"
#endif

/* ------------------------------------------------------------------ */
static constexpr uint32_t BREATH_STEP_MS = 20;
static constexpr uint32_t BLINK_ON_MS    = 200;
static constexpr uint32_t BLINK_OFF_MS   = 300;
static constexpr uint32_t FLASH_ON_MS    = 200;
static constexpr uint32_t FLASH_OFF_MS   = 300;
static constexpr uint32_t FLOW_STEP_MS   = 120;
static constexpr uint32_t CHASE_STEP_MS  = 80;
static constexpr int      BREATH_MIN     = 0;
static constexpr int      BREATH_MAX     = 120;

/* nRF52832 @ 64 MHz — WS2812/SK6812 800 kHz bit periods (approx). */
static constexpr uint32_t T0H_CYCLES = 20;   /* ~0.31 µs */
static constexpr uint32_t T0L_CYCLES = 60;   /* ~0.94 µs */
static constexpr uint32_t T1H_CYCLES = 40;   /* ~0.63 µs */
static constexpr uint32_t T1L_CYCLES = 40;   /* ~0.63 µs */

static inline void delay_cycles(uint32_t n)
{
    while (n--) {
        __NOP();
    }
}

/* ------------------------------------------------------------------ */
LED::LED(uint8_t pin, uint8_t count)
    : m_pin(pin),
      m_count(count > kMaxLeds ? kMaxLeds : count),
      m_is_strip(count > 0),
      m_timing_only(count > 0 && pin == 0),
      m_mirror_onboard(count == 0 || (count > 0 && pin == 0)),
      m_pattern(LedPattern::OFF),
      m_last_ms(0), m_step(0), m_substep(0),
      m_loop_done(false), m_brightness(0), m_dir(1),
      m_last_show_us(0), m_show_count(0)
{
    memset(m_grb, 0, sizeof(m_grb));
    if (m_is_strip && m_count == 0)
        m_is_strip = false;
}

void LED::begin()
{
    if (m_mirror_onboard)
        begin_onboard();
    if (m_is_strip)
        begin_strip();
}

void LED::set_pattern(LedPattern p)
{
    if (m_pattern == p) return;

    if (m_is_strip) {
        strip_clear_buf();
        strip_show();
    }
    if (m_mirror_onboard)
        nicla::leds.setColor(0, 0, 0);

    m_pattern    = p;
    m_step       = 0;
    m_substep    = 0;
    m_last_ms    = 0;
    m_loop_done  = false;
    m_brightness = 0;
    m_dir        = 1;
}

void LED::update()
{
    switch (m_pattern) {
    case LedPattern::OFF:
        return;

    case LedPattern::BLUE_SLOW_FLOW:
    case LedPattern::BLUE_SLOW_FLOW_POST:
        if (m_is_strip) show_strip_blue_flow();
        if (m_mirror_onboard) show_onboard_breathing();
        return;

    case LedPattern::GREEN_CHASE:
        if (m_is_strip) show_strip_chase(0, 60, 0);
        if (m_mirror_onboard) show_onboard_blink(0, 60, 0);
        return;

    case LedPattern::RED_CHASE:
        if (m_is_strip) show_strip_chase(60, 0, 0);
        if (m_mirror_onboard) show_onboard_blink(60, 0, 0);
        return;

    case LedPattern::RED_FLASH_3:
        if (m_is_strip) show_strip_flash3();
        if (m_mirror_onboard) show_onboard_flash3();
        return;
    }
}

/* ================================================================== */
/*  ONBOARD                                                            */
/* ================================================================== */
void LED::begin_onboard()
{
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 0);
}

void LED::show_onboard_breathing()
{
    uint32_t now = millis();
    if (m_last_ms != 0 && now - m_last_ms < BREATH_STEP_MS) return;
    /* When strip also animates, don't steal m_last_ms from chase —
       use independent path only if strip off. Shared m_last_ms is OK
       for mirror because chase step >> breath; for strip+mirror we
       still want onboard blink to show LOGGING. Use pattern-local: */
    if (!m_is_strip) {
        m_last_ms = now;
        m_brightness += m_dir;
        if (m_brightness >= BREATH_MAX) { m_brightness = BREATH_MAX; m_dir = -1; }
        else if (m_brightness <= BREATH_MIN) { m_brightness = BREATH_MIN; m_dir = 1; }
        nicla::leds.setColor(0, 0, m_brightness);
        return;
    }
    /* Strip active + mirror: slow blue breath independent via m_substep unused in flow */
    static uint32_t breath_ms = 0;
    static int br = 0, bd = 1;
    if (breath_ms != 0 && now - breath_ms < BREATH_STEP_MS) return;
    breath_ms = now;
    br += bd;
    if (br >= BREATH_MAX) { br = BREATH_MAX; bd = -1; }
    else if (br <= BREATH_MIN) { br = BREATH_MIN; bd = 1; }
    nicla::leds.setColor(0, 0, br);
}

void LED::show_onboard_blink(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t now = millis();
    static uint32_t blink_ms = 0;
    static uint8_t  blink_ph = 0;
    uint32_t* pms = m_is_strip ? &blink_ms : &m_last_ms;
    uint8_t*  pph = m_is_strip ? &blink_ph : &m_step;
    uint32_t dur = (*pph == 0) ? BLINK_ON_MS : BLINK_OFF_MS;
    if (*pms != 0 && now - *pms < dur) return;
    *pms = now;
    nicla::leds.setColor((*pph == 0) ? r : 0, (*pph == 0) ? g : 0, (*pph == 0) ? b : 0);
    *pph = 1 - *pph;
}

void LED::show_onboard_flash3()
{
    if (m_loop_done && !m_is_strip) return;
    uint32_t now = millis();
    static uint32_t fms = 0;
    static uint8_t  fsub = 0;
    static bool     fdone = false;
    uint32_t* pms = m_is_strip ? &fms : &m_last_ms;
    uint8_t*  psub = m_is_strip ? &fsub : &m_substep;
    bool*     pdone = m_is_strip ? &fdone : &m_loop_done;
    if (*pdone) return;
    uint32_t dur = (*psub % 2 == 0) ? FLASH_ON_MS : FLASH_OFF_MS;
    if (*pms != 0 && now - *pms < dur) return;
    *pms = now;
    nicla::leds.setColor((*psub % 2 == 0) ? 60 : 0, 0, 0);
    (*psub)++;
    if (*psub >= 6) {
        *pdone = true;
        nicla::leds.setColor(0, 0, 0);
    }
}

/* ================================================================== */
/*  SK6812 native                                                      */
/* ================================================================== */
void LED::begin_strip()
{
    memset(m_grb, 0, sizeof(m_grb));
    if (!m_timing_only && m_pin > 0) {
        pinMode(m_pin, OUTPUT);
#if defined(NRF_GPIO)
        /* Prefer direct GPIO if pin maps 1:1 — Arduino pinMode still OK */
#endif
        digitalWrite(m_pin, LOW);
    }
    strip_show(); /* initial dark / timing baseline */
}

void LED::set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i >= m_count) return;
    uint8_t* p = &m_grb[i * 3];
    p[0] = g; p[1] = r; p[2] = b; /* SK6812 GRB */
}

void LED::strip_clear_buf()
{
    memset(m_grb, 0, (size_t)m_count * 3u);
}

void LED::strip_show()
{
    const uint16_t nbytes = (uint16_t)m_count * 3u;
    uint32_t t0 = micros();

    noInterrupts();
    if (m_timing_only) {
        /* Burn equivalent bit time without GPIO — models IRQ-masked show(). */
        for (uint16_t i = 0; i < nbytes; i++) {
            uint8_t v = m_grb[i];
            for (uint8_t b = 0; b < 8; b++) {
                if (v & 0x80) {
                    delay_cycles(T1H_CYCLES);
                    delay_cycles(T1L_CYCLES);
                } else {
                    delay_cycles(T0H_CYCLES);
                    delay_cycles(T0L_CYCLES);
                }
                v <<= 1;
            }
        }
    } else {
        const uint8_t pin = m_pin;
        for (uint16_t i = 0; i < nbytes; i++) {
            uint8_t v = m_grb[i];
            for (uint8_t b = 0; b < 8; b++) {
                if (v & 0x80) {
                    digitalWrite(pin, HIGH);
                    delay_cycles(T1H_CYCLES);
                    digitalWrite(pin, LOW);
                    delay_cycles(T1L_CYCLES);
                } else {
                    digitalWrite(pin, HIGH);
                    delay_cycles(T0H_CYCLES);
                    digitalWrite(pin, LOW);
                    delay_cycles(T0L_CYCLES);
                }
                v <<= 1;
            }
        }
    }
    interrupts();

    /* Latch ≥ 50 µs */
    delayMicroseconds(60);

    m_last_show_us = micros() - t0;
    m_show_count++;
}

void LED::show_strip_blue_flow()
{
    uint32_t now = millis();
    if (m_last_ms != 0 && now - m_last_ms < FLOW_STEP_MS) return;
    m_last_ms = now;

    strip_clear_buf();
    set_pixel(m_step, 0, 0, 80);
    uint8_t prev = (m_step == 0) ? (uint8_t)(m_count - 1) : (uint8_t)(m_step - 1);
    set_pixel(prev, 0, 0, 20);
    strip_show();

    m_step++;
    if (m_step >= m_count) m_step = 0;
}

void LED::show_strip_chase(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t now = millis();
    if (m_last_ms != 0 && now - m_last_ms < CHASE_STEP_MS) return;
    m_last_ms = now;

    strip_clear_buf();
    set_pixel(m_step, r, g, b);
    strip_show();

    m_step++;
    if (m_step >= m_count) m_step = 0;
}

void LED::show_strip_flash3()
{
    if (m_loop_done) return;
    uint32_t now = millis();
    uint32_t dur = (m_substep % 2 == 0) ? FLASH_ON_MS : FLASH_OFF_MS;
    if (m_last_ms != 0 && now - m_last_ms < dur) return;
    m_last_ms = now;

    if (m_substep % 2 == 0) {
        for (uint8_t i = 0; i < m_count; i++) set_pixel(i, 80, 0, 0);
    } else {
        strip_clear_buf();
    }
    strip_show();

    m_substep++;
    if (m_substep >= 6) {
        m_loop_done = true;
        strip_clear_buf();
        strip_show();
    }
}
