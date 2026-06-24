/**
 * @file    ldc1612.cpp
 * @brief   LDC1612 driver — software I²C implementation (no mbed Wire).
 *
 * Uses bit-banged open-drain I²C on P0.22 (SDA) + P0.23 (SCL).
 * The Seeed Grove board has its own 4.7kΩ pull-ups → SDA is released
 * by switching to INPUT (Hi-Z), NEVER actively driven HIGH.
 *
 * Register map: TI LDC1612/LDC1614 datasheet (SNOSCY9B).
 */

#include "ldc1612.h"

/* ═══════════════════════════════════════════════════════════════════
 * I²C Pin constants (file scope — used by static helpers below)
 * ═══════════════════════════════════════════════════════════════════ */

static constexpr uint8_t PIN_SDA = 22;   /* P0.22 */
static constexpr uint8_t PIN_SCL = 23;   /* P0.23 */

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
 * Conversion timing (40 MHz reference clock on Nicla)
 * ═══════════════════════════════════════════════════════════════════ */

/* RCOUNT = 0x0800 → 2048 × 16 / 40 MHz ≈ 819 µs per sample (~1.2 kHz).
 * Fast enough for proximity, moderate power. */
static constexpr uint16_t RCOUNT_VAL         = 0x0800;
static constexpr uint16_t SETTLECOUNT_VAL    = 0x0020;   /* 32 × 16 / 40 MHz ≈ 12.8 µs */
static constexpr uint16_t CLOCK_DIVIDERS     = 0x1001;   /* FIN=1, FREF=1 */

/* Proximity defaults */
static constexpr uint32_t DEFAULT_ARM_THRESHOLD    = 2000000;
static constexpr uint32_t DEFAULT_ARM_HOLD_MS      = 1000;
static constexpr uint32_t DEFAULT_FACTORY_HOLD_MS  = 20000;

/* Poll period */
static constexpr uint32_t TICK_PERIOD_MS     = 20;

/* ═══════════════════════════════════════════════════════════════════
 * Software I²C primitives — strict open-drain emulation
 *
 * CRITICAL: SDA is NEVER actively driven HIGH.
 * To release SDA → pinMode(INPUT). The Seeed board's 4.7kΩ external
 * pull-ups pull the line to 3.3V naturally.
 * To drive SDA LOW → pinMode(OUTPUT) + digitalWrite(LOW).
 * ═══════════════════════════════════════════════════════════════════ */

static inline void sda_high() { pinMode(PIN_SDA, INPUT_PULLUP); }
static inline void sda_low()  { pinMode(PIN_SDA, OUTPUT); digitalWrite(PIN_SDA, LOW); }
static inline void scl_high() { digitalWrite(PIN_SCL, HIGH); }
static inline void scl_low()  { digitalWrite(PIN_SCL, LOW); }

static void i2c_delay() { delayMicroseconds(5); }
static void i2c_half()  { delayMicroseconds(2); }

void LDC1612::i2c_start()
{
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();
}

void LDC1612::i2c_stop()
{
    sda_low();  i2c_delay();
    scl_high(); i2c_delay();
    sda_high();
}

bool LDC1612::i2c_write(uint8_t data)
{
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        if (data & mask) sda_high(); else sda_low();
        i2c_half();
        scl_high(); i2c_delay();
        scl_low();
    }
    /* Read ACK */
    sda_high(); i2c_half();
    scl_high(); i2c_delay();
    bool ack = (digitalRead(PIN_SDA) == LOW);
    scl_low();
    return ack;
}

uint8_t LDC1612::i2c_read(bool ack)
{
    uint8_t val = 0;
    sda_high();
    for (uint8_t i = 0; i < 8; i++) {
        i2c_half();
        scl_high(); i2c_delay();
        val = (val << 1) | (digitalRead(PIN_SDA) ? 1 : 0);
        scl_low();
    }
    /* Send ACK/NACK */
    if (ack) sda_low(); else sda_high();
    i2c_half();
    scl_high(); i2c_delay();
    scl_low();
    sda_high();
    return val;
}

/* ═══════════════════════════════════════════════════════════════════
 * Register I/O
 * ═══════════════════════════════════════════════════════════════════ */

uint16_t LDC1612::read_reg16(uint8_t reg)
{
    if (!m_initialized) return 0xFFFF;

    i2c_start();
    if (!i2c_write(I2C_ADDR << 1)) { i2c_stop(); return 0xFFFF; }  /* write */
    if (!i2c_write(reg))           { i2c_stop(); return 0xFFFF; }  /* register */

    i2c_start();
    if (!i2c_write((I2C_ADDR << 1) | 1)) { i2c_stop(); return 0xFFFF; }  /* read */
    uint8_t hi = i2c_read(true);   /* ACK */
    uint8_t lo = i2c_read(false);  /* NACK */
    i2c_stop();
    return ((uint16_t)hi << 8) | lo;
}

void LDC1612::write_reg16(uint8_t reg, uint16_t val)
{
    if (!m_initialized) return;

    i2c_start();
    if (!i2c_write(I2C_ADDR << 1)) { i2c_stop(); return; }
    i2c_write(reg);
    i2c_write((uint8_t)(val >> 8));
    i2c_write((uint8_t)(val & 0xFF));
    i2c_stop();
}

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
    , m_arm_threshold(DEFAULT_ARM_THRESHOLD)
    , m_arm_hold_ms(DEFAULT_ARM_HOLD_MS)
    , m_factory_hold_ms(DEFAULT_FACTORY_HOLD_MS)
{}

/* ═══════════════════════════════════════════════════════════════════
 * Initialization
 * ═══════════════════════════════════════════════════════════════════ */

bool LDC1612::begin()
{
    /* Configure I²C pins */
    pinMode(PIN_SCL, OUTPUT);
    digitalWrite(PIN_SCL, HIGH);
    pinMode(PIN_SDA, INPUT_PULLUP);  /* internal pull-up — Seeed board's external ones may be absent */

    /* Quick bus check: try a START+address+STOP */
    i2c_start();
    bool ack = i2c_write(I2C_ADDR << 1);
    i2c_stop();
    if (!ack) {
        m_initialized = false;
        return false;
    }

    /* Verify device ID */
    if (!is_connected()) {
        m_initialized = false;
        return false;
    }

    /* ── Configure sensor ── */
    write_reg16(REG_CONFIG, 0x0001);            /* CH0 active, sleep off */
    write_reg16(REG_MUX_CONFIG, 0x0201);        /* CH0, deglitch 1 MHz */
    write_reg16(REG_SETTLECOUNT0, SETTLECOUNT_VAL);
    write_reg16(REG_RCOUNT0, RCOUNT_VAL);
    write_reg16(REG_CLOCK_DIVIDERS0, CLOCK_DIVIDERS);
    write_reg16(REG_DRIVE_CURRENT0, 0x8000);    /* IDRIVE=16 */
    write_reg16(REG_ERROR_CONFIG, 0x0100);      /* DRDY_2INT_CH0 = 1 (FIXED) */

    delay(10);

    /* Auto-calibrate baseline */
    uint32_t sum = 0;
    const int n = 10;
    for (int i = 0; i < n; i++) {
        delay(TICK_PERIOD_MS);
        sum += read_data();
    }
    m_baseline = sum / n;

    m_initialized = true;
    m_last_tick_ms = millis();
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 * Main poll
 * ═══════════════════════════════════════════════════════════════════ */

void LDC1612::tick()
{
    if (!m_initialized) return;

    uint32_t now = millis();
    uint32_t elapsed = now - m_last_tick_ms;
    if (elapsed < TICK_PERIOD_MS) return;
    m_last_tick_ms = now;

    if (m_recalibrate) {
        m_baseline = read_data();
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
    auto now = millis();

    bool prox = false;
    if (raw < m_baseline) {
        uint32_t delta = m_baseline - raw;
        prox = (delta > m_arm_threshold);
    }

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
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Data reading (28-bit from DATA0)
 * ═══════════════════════════════════════════════════════════════════ */

uint32_t LDC1612::read_data()
{
    if (!m_initialized) return 0;

    /* Wait for data ready (STATUS bit 3 = DRDY_CH0) */
    uint32_t timeout = millis() + 50;
    while (!(read_reg16(REG_STATUS) & 0x08)) {
        if (millis() > timeout) return 0;
    }

    uint16_t msb = read_reg16(REG_DATA0_MSB);
    uint16_t lsb = read_reg16(REG_DATA0_LSB);
    return ((uint32_t)msb << 12) | ((lsb >> 4) & 0x0FFF);
}

/* ═══════════════════════════════════════════════════════════════════
 * Recalibration
 * ═══════════════════════════════════════════════════════════════════ */

void LDC1612::force_recalibrate()
{
    m_recalibrate = true;
}

/* ═══════════════════════════════════════════════════════════════════
 * INTB
 * ═══════════════════════════════════════════════════════════════════ */

void LDC1612::enable_interrupt()
{
    pinMode(INTB_PIN, INPUT_PULLUP);
}

/* ═══════════════════════════════════════════════════════════════════
 * Self-test
 * ═══════════════════════════════════════════════════════════════════ */

bool LDC1612::is_connected()
{
    return read_device_id() == 0x3055;
}

uint16_t LDC1612::read_device_id()
{
    return read_reg16(REG_DEVICE_ID);
}

uint16_t LDC1612::read_manufacturer_id()
{
    return read_reg16(REG_MANUFACTURER_ID);
}
