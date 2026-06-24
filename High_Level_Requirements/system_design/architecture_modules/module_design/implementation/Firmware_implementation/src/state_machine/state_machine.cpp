/**
 * @file    state_machine.cpp
 * @brief   SGC state machine implementation.
 *
 * Phase 9: Always-JSON output (ADR-001). No #ifdef on output code.
 */

#include "state_machine.h"
#include "config.h"
#include "test_json.h"
#include <Arduino.h>

/* ------------------------------------------------------------------ */
StateMachine::StateMachine()
    : m_state(DeviceState::SLEEP),
      m_state_entered_ms(0),
      m_allow_rearm(true),
      m_cooldown_notified(false)
{
}

/* ------------------------------------------------------------------ */
const char* StateMachine::state_name() const
{
    return state_name_for(m_state);
}

const char* StateMachine::state_name_for(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP:    return "SLEEP";
    case DeviceState::IDLE:     return "IDLE";
    case DeviceState::ARMED:    return "ARMED";
    case DeviceState::LOGGING:  return "LOGGING";
    case DeviceState::POST_RUN: return "POST_RUN";
    default:                    return "?";
    }
}

/* ------------------------------------------------------------------ */
bool StateMachine::can_arm() const
{
    return (m_state == DeviceState::IDLE) || m_allow_rearm;
}

/* ------------------------------------------------------------------ */
void StateMachine::force_state(DeviceState s)
{
    if (s == m_state) return;

    switch (s) {
    case DeviceState::SLEEP:
    case DeviceState::IDLE:
        break;
    case DeviceState::ARMED:
        if (!can_arm()) {
            json_begin();
            json_kv("ev", "arm_blocked");
            Serial.print(','); json_kv("reason", "cooldown");
            json_end();
            return;
        }
        break;
    case DeviceState::LOGGING:
        if (m_state != DeviceState::ARMED) {
            json_begin();
            json_kv("ev", "state_blocked");
            Serial.print(','); json_kv("reason", "not_armed");
            Serial.print(','); json_kv("current", state_name());
            json_end();
            return;
        }
        break;
    case DeviceState::POST_RUN:
        if (m_state != DeviceState::LOGGING) {
            json_begin();
            json_kv("ev", "state_blocked");
            Serial.print(','); json_kv("reason", "not_logging");
            Serial.print(','); json_kv("current", state_name());
            json_end();
            return;
        }
        break;
    }

    enter_state(s);
}

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
    case DeviceState::LOGGING:
        break;
    case DeviceState::POST_RUN:
        m_allow_rearm = false;
        m_cooldown_notified = false;
        break;
    }
}

/* ------------------------------------------------------------------ */
void StateMachine::tick()
{
    check_timeouts();
}

/* ------------------------------------------------------------------ */
void StateMachine::check_timeouts()
{
    uint32_t elapsed = millis() - m_state_entered_ms;

    switch (m_state) {
    case DeviceState::IDLE:
        if (elapsed >= SLEEP_TIMEOUT_MS) {
            json_begin();
            json_kv("ev", "timeout");
            Serial.print(','); json_kv("from", "IDLE");
            Serial.print(','); json_kv("to", "SLEEP");
            json_end();
            enter_state(DeviceState::SLEEP);
        }
        break;

    case DeviceState::ARMED:
        if (elapsed >= ARM_TIMEOUT_MS) {
            json_begin();
            json_kv("ev", "timeout");
            Serial.print(','); json_kv("from", "ARMED");
            Serial.print(','); json_kv("to", "IDLE");
            json_end();
            enter_state(DeviceState::IDLE);
        }
        break;

    case DeviceState::POST_RUN:
        if (elapsed >= POST_RUN_COOLDOWN_MS && !m_cooldown_notified) {
            m_cooldown_notified = true;
            json_begin();
            json_kv("ev", "cooldown");
            Serial.print(','); json_kv("from", "POST_RUN");
            Serial.print(','); json_kv("to", "IDLE");
            json_end();
            enter_state(DeviceState::IDLE);
        }
        if (elapsed >= SLEEP_TIMEOUT_MS) {
            json_begin();
            json_kv("ev", "timeout");
            Serial.print(','); json_kv("from", "POST_RUN");
            Serial.print(','); json_kv("to", "SLEEP");
            json_end();
            enter_state(DeviceState::SLEEP);
        }
        break;

    case DeviceState::LOGGING:
        if (elapsed >= MAX_LOG_DURATION_MS) {
            json_begin();
            json_kv("ev", "timeout");
            Serial.print(','); json_kv("from", "LOGGING");
            Serial.print(','); json_kv("to", "POST_RUN");
            json_end();
            enter_state(DeviceState::POST_RUN);
        }
        break;

    case DeviceState::SLEEP:
        break;
    }
}
