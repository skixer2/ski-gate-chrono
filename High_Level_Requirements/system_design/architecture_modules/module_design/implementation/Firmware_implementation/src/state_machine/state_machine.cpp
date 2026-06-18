/**
 * @file    state_machine.cpp
 * @brief   SGC state machine implementation.
 */

#include "state_machine.h"
#include "config.h"
#include <Arduino.h>

/* ------------------------------------------------------------------ */
/*  Constructor                                                        */
/* ------------------------------------------------------------------ */
StateMachine::StateMachine()
    : m_state(DeviceState::SLEEP),
      m_state_entered_ms(0),
      m_allow_rearm(true),
      m_cooldown_notified(false)
{
}

/* ------------------------------------------------------------------ */
/*  State names for debug                                              */
/* ------------------------------------------------------------------ */
const char* StateMachine::state_name() const
{
    switch (m_state) {
    case DeviceState::SLEEP:    return "SLEEP";
    case DeviceState::IDLE:     return "IDLE";
    case DeviceState::ARMED:    return "ARMED";
    case DeviceState::LOGGING:  return "LOGGING";
    case DeviceState::POST_RUN: return "POST_RUN";
    default:                    return "?";
    }
}

/* ------------------------------------------------------------------ */
/*  can_arm                                                            */
/* ------------------------------------------------------------------ */
bool StateMachine::can_arm() const
{
    return (m_state == DeviceState::IDLE) || m_allow_rearm;
}

/* ------------------------------------------------------------------ */
/*  force_state — for serial-command testing                           */
/* ------------------------------------------------------------------ */
void StateMachine::force_state(DeviceState s)
{
    if (s == m_state) return;

    /* Validate transitions */
    switch (s) {
    case DeviceState::SLEEP:
        /* Any state can go to SLEEP */
        break;
    case DeviceState::IDLE:
        /* Any state can go to IDLE (via timeout or manual) */
        break;
    case DeviceState::ARMED:
        if (!can_arm()) {
            Serial.println("  (blocked: POST_RUN cooldown active)");
            return;
        }
        break;
    case DeviceState::LOGGING:
        if (m_state != DeviceState::ARMED) {
            Serial.println("  (blocked: must be ARMED first)");
            return;
        }
        break;
    case DeviceState::POST_RUN:
        if (m_state != DeviceState::LOGGING) {
            Serial.println("  (blocked: must be LOGGING first)");
            return;
        }
        break;
    }

    enter_state(s);
}

/* ------------------------------------------------------------------ */
/*  enter_state                                                        */
/* ------------------------------------------------------------------ */
void StateMachine::enter_state(DeviceState s)
{
    m_state = s;
    m_state_entered_ms = millis();

    switch (s) {
    case DeviceState::SLEEP:
        m_allow_rearm = true;
        break;
    case DeviceState::IDLE:
        m_allow_rearm = true;
        break;
    case DeviceState::ARMED:
        break;
    case DeviceState::LOGGING:
        break;
    case DeviceState::POST_RUN:
        m_allow_rearm = false;
        m_cooldown_notified = false;
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  tick — handle timeouts                                             */
/* ------------------------------------------------------------------ */
void StateMachine::tick()
{
    check_timeouts();
}

/* ------------------------------------------------------------------ */
/*  check_timeouts                                                     */
/* ------------------------------------------------------------------ */
void StateMachine::check_timeouts()
{
    uint32_t elapsed = millis() - m_state_entered_ms;

    switch (m_state) {
    case DeviceState::IDLE:
        if (elapsed >= SLEEP_TIMEOUT_MS) {
            Serial.println("  (timeout: IDLE → SLEEP)");
            enter_state(DeviceState::SLEEP);
        }
        break;

    case DeviceState::ARMED:
        if (elapsed >= ARM_TIMEOUT_MS) {
            Serial.println("  (timeout: ARMED → IDLE)");
            enter_state(DeviceState::IDLE);
        }
        break;

    case DeviceState::POST_RUN:
        if (elapsed >= POST_RUN_COOLDOWN_MS && !m_cooldown_notified) {
            m_cooldown_notified = true;
            Serial.println("  (POST_RUN cooldown complete → IDLE)");
            enter_state(DeviceState::IDLE);
        }
        /* POST_RUN also has a sleep timeout like IDLE */
        if (elapsed >= SLEEP_TIMEOUT_MS) {
            Serial.println("  (timeout: POST_RUN → SLEEP)");
            enter_state(DeviceState::SLEEP);
        }
        break;

    case DeviceState::LOGGING:
        /* LOGGING runs until explicit stop (or battery low) */
        break;

    case DeviceState::SLEEP:
        /* SLEEP persists until woken */
        break;
    }
}
