/**
 * @file    ldc1612.h
 * @brief   LDC1612 inductive proximity sensor driver — hardware Wire edition.
 *
 * I2C0 / Wire (P0.22 SDA, P0.23 SCL) at address 0x2B (Seeed default).
 * INTB → P0.02 (A0 header) for wake-from-sleep.
 *
 * F03 — 1000 ms continuous proximity → arming trigger.
 * F13 — INTB GPIO interrupt for wake-from-sleep.
 * F42 — 20 s continuous hold → factory reset.
 *
 * Phase 11 fix:
 *   - EWMA auto-recalibration: baseline slowly tracks ambient drift when no
 *     target is present (α = 1/64, τ ≈ 1.3 s).  Eliminates false proximity
 *     from temperature drift of the external LC tank.
 *   - Dead-baseline guard: begin() rejects baseline == 0, which would cause
 *     an infinite wipe-reboot loop (any real reading would delta >> threshold).
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

class LDC1612
{
public:
    enum class Status : uint8_t { NO_TARGET = 0, APPROACHING = 1, ARMED = 2, FACTORY_HOLD = 3 };

    LDC1612();

    bool begin();
    void tick();

    Status   status()          const { return m_status; }
    uint32_t proximity_ms()    const { return m_proximity_ms; }
    uint32_t data()            const { return m_data; }
    uint32_t baseline()        const { return m_baseline; }
    bool     baseline_valid()  const { return m_baseline_valid; }
    bool     is_armed()        const { return m_status == Status::ARMED; }
    bool     is_factory_hold() const { return m_status == Status::FACTORY_HOLD; }
    bool     is_proximity()    const { return m_status != Status::NO_TARGET; }

    void set_arm_threshold(uint32_t thresh) { m_arm_threshold = thresh; }
    void set_hold_ms(uint32_t ms)           { m_arm_hold_ms = ms; }
    void set_factory_hold_ms(uint32_t ms)   { m_factory_hold_ms = ms; }
    void force_recalibrate();
    void enable_interrupt();

    bool     is_connected();
    uint16_t read_device_id();
    uint16_t read_manufacturer_id();

private:
    uint16_t read_reg16(uint8_t reg);
    void     write_reg16(uint8_t reg, uint16_t val);
    uint32_t read_data();
    void     detect_proximity(uint32_t raw);

    Status   m_status;
    uint32_t m_data, m_baseline, m_proximity_ms;
    uint32_t m_last_tick_ms, m_last_prox_ms;
    bool     m_initialized, m_recalibrate, m_wire_ok;
    bool     m_baseline_valid;               /* Phase 11: true if baseline is non-zero   */
    uint32_t m_arm_threshold, m_arm_hold_ms, m_factory_hold_ms;

    static constexpr uint8_t INTB_PIN = 2;
};
