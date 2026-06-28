/**
 * @file    ldc1612.cpp
 * @brief   LDC1612 driver — hardware Wire I²C on Nicla (P0.22 SDA, P0.23 SCL).
 *
 * CRITICAL: The mbed Wire driver HardFaults if called after a NACK.
 * Every Wire transaction checks for errors and stops using Wire if
 * a failure occurs (m_wire_ok = false). Once Wire is marked bad, the
 * driver enters silent-fail mode — no further I²C until reboot.
 *
 * Register map: TI LDC1612/LDC1614 datasheet (SNOSCY9B).
 *
 * Phase 11 — False-proximity mitigation (2026-06-28):
 *   1. Dead-baseline guard: begin() rejects m_baseline == 0.
 *   2. EWMA auto-recalibration: when NO_TARGET, baseline slowly tracks the
 *      raw reading (α = 1/64, τ ≈ 1.3 s).  Ambient thermal drift of the
 *      external LC tank is absorbed; a real metal target (Δ thousands of
 *      counts) comfortably exceeds the arming threshold.
 *   3. ARM_THRESHOLD raised from 100 → 500.  With EWMA tracking, noise +
 *      residual drift stays well under 500; a real target is unambiguous.
 */

#include "ldc1612.h"
#include <Wire.h>

/* ═══════════════════════════════════════════════════════════════════
 * LDC1612 Register Addresses
 * ═══════════════════════════════════════════════════════════════════ */

static constexpr uint8_t REG_DATA0_MSB       = 0x00;
static constexpr uint8_t REG_DATA0_LSB       = 0x01;
static constexpr uint8_t REG_RCOUNT0         = 0x08;
static constexpr uint8_t REG_SETTLECOUNT0    = 0x10;
static constexpr uint8_t REG_CLOCK_DIVIDERS0 = 0x14;
static constexpr uint8_t REG_STATUS          = 0x18;
static constexpr uint8_t REG_ERROR_CONFIG    = 0x19;
static constexpr uint8_t REG_CONFIG          = 0x1A;
static constexpr uint8_t REG_MUX_CONFIG      = 0x1B;
static constexpr uint8_t REG_DRIVE_CURRENT0  = 0x1E;
static constexpr uint8_t REG_MANUFACTURER_ID = 0x7E;
static constexpr uint8_t REG_DEVICE_ID       = 0x7F;

/* ═══════════════════════════════════════════════════════════════════
 * Conversion timing (40 MHz ref clock)
 * ═══════════════════════════════════════════════════════════════════ */

static constexpr uint16_t RCOUNT_VAL         = 0x0800;   /* 2048 × 16 / 40 MHz ≈ 819 µs */
static constexpr uint16_t SETTLECOUNT_VAL    = 0x0020;   /* 32 × 16 / 40 MHz ≈ 13 µs */
static constexpr uint16_t CLOCK_DIVIDERS     = 0x1001;   /* FIN=1, FREF=1 */

/* Proximity defaults — detects both ferrous (drop) and non-ferrous (rise).
 * Phase 11: ARM_THRESHOLD raised 100→500.  With EWMA auto-recalibration,
 * thermal drift is absorbed below this level; a real metal target produces
 * thousands of counts of shift. */
static constexpr uint32_t DEFAULT_ARM_THRESHOLD    = 500;
static constexpr uint32_t DEFAULT_ARM_HOLD_MS      = 1000;
static constexpr uint32_t DEFAULT_FACTORY_HOLD_MS  = 20000;
static constexpr uint32_t TICK_PERIOD_MS           = 20;

/* Phase 11: EWMA auto-recalibration — time constant ≈ 1.3 s at 20 ms ticks.
 * Shifts of 1/64 per tick are invisible against a real target (Δ ≫ 500),
 * but absorb the ~0.5–3 count/min drift of the LC tank with temperature. */
static constexpr uint8_t  BASELINE_EWMA_SHIFT      = 6;       /* ÷64 per tick */

/* I²C */
static constexpr uint8_t  I2C_ADDR = 0x2B;
static constexpr uint32_t I2C_CLK  = 100000;

/* ═══════════════════════════════════════════════════════════════════
 * Construction
 * ═══════════════════════════════════════════════════════════════════ */

LDC1612::LDC1612()
    : m_status(Status::NO_TARGET)
    , m_data(0)
    , m_baseline(0)
    , m_proximity_ms(0)
    , m_last_tick_ms(0)
    , m_last_prox_ms(0)
    , m_initialized(false)
    , m_recalibrate(false)
    , m_wire_ok(true)
    , m_baseline_valid(false)
    , m_arm_threshold(DEFAULT_ARM_THRESHOLD)
    , m_arm_hold_ms(DEFAULT_ARM_HOLD_MS)
    , m_factory_hold_ms(DEFAULT_FACTORY_HOLD_MS)
{}

/* ═══════════════════════════════════════════════════════════════════
 * Safe Wire helpers — never call Wire after a failure
 * ═══════════════════════════════════════════════════════════════════ */

uint16_t LDC1612::read_reg16(uint8_t reg)
{
    if (!m_wire_ok) return 0xFFFF;

    Wire.beginTransmission(I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { m_wire_ok = false; return 0xFFFF; }

    if (Wire.requestFrom(I2C_ADDR, (uint8_t)2) != 2) { m_wire_ok = false; return 0xFFFF; }

    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    return ((uint16_t)hi << 8) | lo;
}

void LDC1612::write_reg16(uint8_t reg, uint16_t val)
{
    if (!m_wire_ok) return;

    Wire.beginTransmission(I2C_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)(val & 0xFF));
    if (Wire.endTransmission() != 0) m_wire_ok = false;
}

/* ═══════════════════════════════════════════════════════════════════
 * Initialization
 * ═══════════════════════════════════════════════════════════════════ */

bool LDC1612::begin()
{
    Wire.begin();
    Wire.setClock(I2C_CLK);

    /* Ping device */
    Wire.beginTransmission(I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        m_wire_ok = false;
        return false;
    }

    if (!is_connected()) {
        m_wire_ok = false;
        return false;
    }

    /* Configure sensor */
    write_reg16(REG_CONFIG, 0x0001);
    write_reg16(REG_MUX_CONFIG, 0x0201);
    write_reg16(REG_SETTLECOUNT0, SETTLECOUNT_VAL);
    write_reg16(REG_RCOUNT0, RCOUNT_VAL);
    write_reg16(REG_CLOCK_DIVIDERS0, CLOCK_DIVIDERS);
    write_reg16(REG_DRIVE_CURRENT0, 0x8000);
    write_reg16(REG_ERROR_CONFIG, 0x0100);     /* DRDY_2INT_CH0 = 1 */

    delay(10);

    /* Auto-calibrate baseline — reject if all-zero.
     * Phase 11: m_baseline=0 means read_data() timed out every time
     * (sensor not ready or I²C glitch).  Using 0 would guarantee
     * false proximity on the very first real reading → infinite wipe loop. */
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        delay(TICK_PERIOD_MS);
        sum += read_data();
    }
    m_baseline = sum / 10;
    m_baseline_valid = (m_baseline != 0);

    if (!m_baseline_valid) {
        m_wire_ok = false;        /* treat as permanent until reboot */
        return false;
    }

    m_initialized = true;
    m_last_tick_ms = millis();
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 * Main poll
 * ═══════════════════════════════════════════════════════════════════ */

void LDC1612::tick()
{
    if (!m_initialized || !m_wire_ok) return;

    uint32_t now = millis();
    if ((now - m_last_tick_ms) < TICK_PERIOD_MS) return;
    m_last_tick_ms = now;

    if (m_recalibrate) {
        uint32_t raw = read_data();
        if (raw != 0) {
            m_baseline = raw;
            m_baseline_valid = true;
        }
        m_recalibrate = false;
        m_status = Status::NO_TARGET;
        m_proximity_ms = 0;
        return;
    }

    m_data = read_data();
    detect_proximity(m_data);
}

void LDC1612::detect_proximity(uint32_t raw)
{
    if (raw == 0) return;  /* Phase 11: skip dead read — sensor not ready */

    auto now = millis();
    /* Handle both ferrous (raw drops) and non-ferrous (raw rises) targets */
    int64_t delta = (int64_t)raw - (int64_t)m_baseline;
    bool prox = (delta > (int64_t)m_arm_threshold) || (delta < -(int64_t)m_arm_threshold);

    if (prox) {
        if (m_status == Status::NO_TARGET) {
            m_status = Status::APPROACHING;
            m_proximity_ms = 0;
            m_last_prox_ms = now;
        } else {
            m_proximity_ms += (now - m_last_prox_ms);
            m_last_prox_ms = now;
        }
        if (m_proximity_ms >= m_factory_hold_ms)
            m_status = Status::FACTORY_HOLD;
        else if (m_proximity_ms >= m_arm_hold_ms)
            m_status = Status::ARMED;
    } else {
        m_status = Status::NO_TARGET;
        m_proximity_ms = 0;

        /* Phase 11: EWMA auto-recalibration — slowly track ambient drift.
         * When no target is present, pull baseline toward current reading
         * by 1/64 per tick.  Time constant ≈ 1.3 s.
         * This absorbs thermal drift of the external LC tank (copper coil
         * ~0.4%/°C, capacitor tempco) while remaining invisible against
         * a true metal target (Δ thousands of counts in <1 s). */
        int64_t ewma = (int64_t)m_baseline +
                       (((int64_t)raw - (int64_t)m_baseline) >> BASELINE_EWMA_SHIFT);
        m_baseline = (uint32_t)ewma;
        m_baseline_valid = true;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Data reading (28-bit from DATA0)
 * ═══════════════════════════════════════════════════════════════════ */

uint32_t LDC1612::read_data()
{
    if (!m_wire_ok) return 0;

    /* Wait for DRDY (STATUS bit 3) */
    uint32_t timeout = millis() + 50;
    while (!(read_reg16(REG_STATUS) & 0x08)) {
        if (millis() > timeout) return 0;
    }

    uint16_t msb = read_reg16(REG_DATA0_MSB);
    uint16_t lsb = read_reg16(REG_DATA0_LSB);
    return ((uint32_t)msb << 12) | ((lsb >> 4) & 0x0FFF);
}

/* ═══════════════════════════════════════════════════════════════════
 * Misc
 * ═══════════════════════════════════════════════════════════════════ */

void LDC1612::force_recalibrate() { m_recalibrate = true; }
void LDC1612::enable_interrupt()  { pinMode(INTB_PIN, INPUT_PULLUP); }

bool LDC1612::is_connected()         { return read_device_id() == 0x3055; }
uint16_t LDC1612::read_device_id()   { return read_reg16(REG_DEVICE_ID); }
uint16_t LDC1612::read_manufacturer_id() { return read_reg16(REG_MANUFACTURER_ID); }
