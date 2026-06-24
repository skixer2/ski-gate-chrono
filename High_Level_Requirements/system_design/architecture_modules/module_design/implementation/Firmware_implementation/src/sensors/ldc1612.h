/**
 * @file    ldc1612.h
 * @brief   LDC1612 inductive proximity sensor driver — software I²C edition.
 *
 * Uses bit-banged I²C on I2C0 pins (P0.22 SDA, P0.23 SCL) at address 0x2B
 * (Seeed Grove default; 0x2A if ADDR pad bridged to GND).
 * INTB → P0.02 (A0 header) for wake-from-sleep.
 *
 * NO hardware Wire/HAL — zero HardFault risk on mbed Nicla.
 *
 * F03 — 1000 ms continuous proximity → arming trigger.
 * F13 — INTB GPIO interrupt for wake-from-sleep.
 * F42 — 20 s continuous hold → factory reset.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

class LDC1612
{
public:
    enum class Status : uint8_t
    {
        NO_TARGET    = 0,
        APPROACHING  = 1,
        ARMED        = 2,
        FACTORY_HOLD = 3,
    };

    LDC1612();

    /* Lifecycle */
    bool begin();
    void tick();       /* call every ~20 ms in loop */

    /* Accessors */
    Status   status()     const { return m_status; }
    uint32_t proximity_ms() const { return m_proximity_ms; }
    uint32_t data()       const { return m_data; }
    bool     is_armed()   const { return m_status == Status::ARMED; }
    bool     is_factory_hold() const { return m_status == Status::FACTORY_HOLD; }

    /* Configuration */
    void set_arm_threshold(uint32_t thresh) { m_arm_threshold = thresh; }
    void set_hold_ms(uint32_t ms)           { m_arm_hold_ms = ms; }
    void set_factory_hold_ms(uint32_t ms)   { m_factory_hold_ms = ms; }
    void force_recalibrate();

    /* Self-test */
    bool     is_connected();
    uint16_t read_device_id();
    uint16_t read_manufacturer_id();

    /* INTB */
    void enable_interrupt();

private:
    /* ── Software I²C primitives ── */
    static constexpr uint8_t I2C_ADDR = 0x2B;

    void     i2c_start();
    void     i2c_stop();
    bool     i2c_write(uint8_t data);     /* returns true if ACK */
    uint8_t  i2c_read(bool ack);

    uint16_t read_reg16(uint8_t reg);
    void     write_reg16(uint8_t reg, uint16_t val);
    uint32_t read_data();
    void     detect_proximity(uint32_t raw);

    /* ── State ── */
    Status   m_status;
    uint32_t m_data;
    uint32_t m_baseline;
    uint32_t m_proximity_ms;
    uint32_t m_last_tick_ms;
    uint32_t m_last_prox_ms;
    bool     m_initialized;
    bool     m_recalibrate;

    /* ── Config ── */
    uint32_t m_arm_threshold;
    uint32_t m_arm_hold_ms;
    uint32_t m_factory_hold_ms;

    /* ── INTB ── */
    static constexpr uint8_t INTB_PIN = 2;
};
