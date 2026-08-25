/**
 * @file    state_machine.h
 * @brief   SGC device state machine — SD §1 transitions.
 *
 *   SLEEP ──proximity──→ ARMED ──descent──→ LOGGING ──flatline──→ POST_RUN ──cooldown──→ SLEEP
 *    │                                                       │
 *    └──timeout──→ SLEEP                                    └──timeout──→ POST_RUN
 *
 * V5.19: IDLE state removed. SLEEP is the primary waiting state.
 * Phase 5: transitions are triggered via serial commands (no real sensors yet).
 * V4.41: on_transition callback makes state transitions synchronous —
 *        handlers run immediately inside force_state(), eliminating
 *        the one-iteration race between state change and handler execution.
 */

#pragma once

#include <stdint.h>

enum class DeviceState : uint8_t
{
    SLEEP    = 0xFF,  /* Primary waiting state — ADV + sensors warm */
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

    /** V5.00: while BLE central is connected (or FT active), do not SLEEP→SYSTEM_OFF. */
    void set_hold_sleep(bool hold) { m_hold_sleep = hold; }
    bool hold_sleep() const { return m_hold_sleep; }

    /** V5.47: reset the SLEEP→System-Off timer to the current millis().
     *  Called on BLE disconnect so a connection that lasted e.g. 50 min does
     *  not cause an immediate System Off because the original SLEEP entry
     *  timestamp is now older than SLEEP_SYSTEM_OFF_MS.  Only meaningful in
     *  SLEEP — other states set their own timer on entry. */
    void reset_sleep_timer() {
        if (m_state == DeviceState::SLEEP) m_state_entered_ms = millis();
    }

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
    bool        m_hold_sleep;       /* BLE connected / FT — stay SLEEP (prevent SYSTEM_OFF) */
    TransitionCallback m_on_transition;
};
