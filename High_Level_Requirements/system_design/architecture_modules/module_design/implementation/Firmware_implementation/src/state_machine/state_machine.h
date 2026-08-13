/**
 * @file    state_machine.h
 * @brief   SGC device state machine — SD §1 transitions.
 *
 *   SLEEP  ←timeout──── IDLE ──proximity──→ ARMED ──descent──→ LOGGING ──flatline──→ POST_RUN
 *     ↑                    ↑                    │                                  │
 *     └──timeout───────────┘                    └───timeout──→ IDLE                 └──cooldown──→ IDLE
 *
 * Phase 5: transitions are triggered via serial commands (no real sensors yet).
 * V4.41: on_transition callback makes state transitions synchronous —
 *        handlers run immediately inside force_state(), eliminating
 *        the one-iteration race between state change and handler execution.
 */

#pragma once

#include <stdint.h>

enum class DeviceState : uint8_t
{
    SLEEP    = 0xFF,  /* BLE-off during sleep — not exposed via ABC4 */
    IDLE     = 0,
    ARMED    = 1,
    LOGGING  = 2,
    POST_RUN = 3,
};

/** Callback type: called synchronously when state changes. */
typedef void (*TransitionCallback)(DeviceState from, DeviceState to);

class StateMachine
{
public:
    StateMachine();

    /** Force a specific state. Calls the on_transition callback if set. */
    void force_state(DeviceState s);

    /** Set the transition callback (called synchronously from force_state). */
    void on_transition(TransitionCallback cb) { m_on_transition = cb; }

    /** Called every loop iteration — handles timeouts */
    void tick();

    /** V5.00: while BLE central is connected (or FT active), do not IDLE→SLEEP. */
    void set_hold_idle(bool hold) { m_hold_idle = hold; }
    bool hold_idle() const { return m_hold_idle; }

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
    bool        m_hold_idle;          /* BLE connected / FT — stay IDLE */
    TransitionCallback m_on_transition;
};
