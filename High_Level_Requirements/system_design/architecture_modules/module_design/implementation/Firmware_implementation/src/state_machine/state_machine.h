/**
 * @file    state_machine.h
 * @brief   SGC device state machine — SD §1 transitions.
 *
 *   SLEEP  ←timeout──── IDLE ──proximity──→ ARMED ──descent──→ LOGGING ──flatline──→ POST_RUN
 *     ↑                    ↑                    │                                  │
 *     └──timeout───────────┘                    └───timeout──→ IDLE                 └──cooldown──→ IDLE
 *
 * Phase 5: transitions are triggered via serial commands (no real sensors yet).
 */

#pragma once

#include <stdint.h>

enum class DeviceState : uint8_t
{
    SLEEP    = 0,
    IDLE     = 1,
    ARMED    = 2,
    LOGGING  = 3,
    POST_RUN = 4,
};

class StateMachine
{
public:
    StateMachine();

    /* Force a specific state (for serial-command testing) */
    void force_state(DeviceState s);

    /* Called every loop iteration — handles timeouts */
    void tick();

    /* Accessors */
    DeviceState state() const { return m_state; }
    uint32_t    state_entered_ms() const { return m_state_entered_ms; }
    bool        can_arm() const;
    const char* state_name() const;
    static const char* state_name_for(DeviceState s);

private:
    void enter_state(DeviceState s);
    void check_timeouts();

    DeviceState m_state;
    uint32_t    m_state_entered_ms;
    bool        m_allow_rearm;
    bool        m_cooldown_notified;   /* one-shot: avoid cooldown message spam */
};
