/**
 * @file    piezo_button.cpp
 * @brief   Piezo pushbutton driver — GPIO edge interrupt on P0.02.
 *
 * Replaces LDC1612 for arming and System Off wake (2026-08-22).
 */

#include "piezo_button.h"
#include "nrf.h"
#include "nrf_gpio.h"

PiezoButton g_button;

/* ISR — minimal work, just record timestamp and set flag */
static void button_isr()
{
    uint32_t now = millis();
    PiezoButton *b = &g_button;

    /* Read pin state */
    bool pin_low = (NRF_GPIO->IN >> PiezoButton::PIN) & 1 ? false : true;

    /* Debounce — ignore edges within DEBOUNCE_MS */
    if (now - b->m_last_edge_ms < b->DEBOUNCE_MS) return;
    b->m_last_edge_ms = now;

    /* Only count falling edge (HIGH → LOW = press) */
    if (pin_low && b->m_last_pin_state) {
        b->m_pressed = true;
        b->m_last_press_ms = now;

        /* Factory reset window */
        if (b->m_press_count == 0 || (now - b->m_window_start) > b->PRESS_WINDOW_MS) {
            b->m_window_start = now;
            b->m_press_count = 1;
        } else {
            b->m_press_count++;
            if (b->m_press_count >= b->FACTORY_PRESS_COUNT) {
                b->m_factory_trigger = true;
            }
        }
    }
    b->m_last_pin_state = pin_low;
}

void PiezoButton::begin()
{
    pinMode(PIN, INPUT_PULLUP);
    m_pressed = false;
    m_last_press_ms = 0;
    m_press_count = 0;
    m_window_start = 0;
    m_factory_trigger = false;
    m_last_edge_ms = 0;
    m_last_pin_state = digitalRead(PIN) == HIGH;

    /* Attach falling-edge interrupt */
    attachInterrupt(digitalPinToInterrupt(PIN), button_isr, FALLING);
}

void PiezoButton::tick()
{
    /* Expire factory reset window if timed out */
    uint32_t now = millis();
    if (m_press_count > 0 && (now - m_window_start) > PRESS_WINDOW_MS) {
        if (m_press_count < FACTORY_PRESS_COUNT) {
            m_press_count = 0;
        }
    }
}
