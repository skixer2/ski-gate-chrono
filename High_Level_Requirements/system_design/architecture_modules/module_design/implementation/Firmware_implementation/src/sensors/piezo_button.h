/**
 * @file    piezo_button.h
 * @brief   Piezo pushbutton driver — GPIO edge interrupt on P0.02.
 *
 * Replaces LDC1612 for arming and System Off wake (2026-08-22).
 *
 * Hardware: Langir 16mm piezo, momentary NO, IP68.
 *   - Button → P0.02 (shared with former LDC INTB pin)
 *   - Internal pull-up, active-low (press = LOW)
 *   - GPIO SENSE_LOW for System Off wake
 *
 * Behavior:
 *   - Single press in SLEEP → ARMED
 *   - 5 presses in 3s in SLEEP → factory reset
 *   - System Off wake via GPIO SENSE → auto-ARMED
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

class PiezoButton
{
public:
    /* NB: On Nicla Sense ME, Arduino pin 2 = P0.20 (TX!), not P0.02.
       P0.02 is Arduino pin 10 (A0). Use ARDUINO_PIN for Arduino API
       calls (pinMode, attachInterrupt) and NRF_PIN for direct register
       access (NRF_GPIO->IN, PIN_CNF). */
    static constexpr uint8_t  ARDUINO_PIN     = 10;  /* A0 = P0.02 */
    static constexpr uint8_t  NRF_PIN         = 2;   /* P0.02 physical */
    static constexpr uint32_t DEBOUNCE_MS     = 20;      /* mechanical debounce */
    static constexpr uint32_t PRESS_WINDOW_MS = 3000;    /* 5 presses in 3s = factory reset */
    static constexpr uint8_t  FACTORY_PRESS_COUNT = 5;

    void begin();
    void tick();

    /* Check if a press was detected (call from loop, non-stream, non-LOGGING) */
    bool is_pressed() const { return m_pressed; }

    /* Consume the press event */
    void clear_press() { m_pressed = false; }

    /* Factory reset: returns true when PRESS_COUNT presses detected in window */
    bool is_factory_trigger() const { return m_factory_trigger; }
    void clear_factory_trigger() { m_factory_trigger = false; }

    /* Press count for diagnostics */
    uint8_t press_count() const { return m_press_count; }

private:
    volatile bool   m_pressed;          /* set by ISR, consumed by loop */
    volatile uint32_t m_last_press_ms;  /* timestamp of last valid press */
    volatile uint8_t  m_press_count;    /* presses in current window */
    volatile uint32_t m_window_start;   /* start of press window */
    volatile bool     m_factory_trigger;

    /* Debounce state */
    volatile uint32_t m_last_edge_ms;
    volatile bool     m_last_pin_state;  /* HIGH = released, LOW = pressed */

    /* ISR is a free function — needs access to private state */
    friend void button_isr();
};

/* Global singleton */
extern PiezoButton g_button;
