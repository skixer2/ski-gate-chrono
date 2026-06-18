/**
 * @file    spi_flash.cpp
 * @brief   MX25R1635F via mbed::BlockDevice::get_default_instance().
 *          This is the canonical Nicla Sense ME approach — same as
 *          StandaloneFlashStorage.ino (nicla-sense-me-fw).
 */

#include "spi_flash.h"
#include <Arduino.h>
#include <BlockDevice.h>

/* ------------------------------------------------------------------ */
SPIFlash::SPIFlash()
    : m_bd(nullptr), m_ok(false)
{
}

/* ------------------------------------------------------------------ */
bool SPIFlash::begin()
{
    m_bd = mbed::BlockDevice::get_default_instance();
    if (!m_bd) {
        Serial.println("SPI Flash: get_default_instance() returned null");
        return false;
    }

    int rc = static_cast<mbed::BlockDevice*>(m_bd)->init();
    if (rc != 0) {
        Serial.print("SPI Flash: init() failed, rc=");
        Serial.println(rc);
        return false;
    }

    Serial.print("SPI Flash: ");
    Serial.print(total_size() / 1024 / 1024);
    Serial.print(" MB, block ");
    Serial.print(block_size());
    Serial.print(" B, page ");
    Serial.print(page_size());
    Serial.println(" B");

    m_ok = true;

    /* Run self-test on init */
    return self_test();
}

/* ------------------------------------------------------------------ */
uint32_t SPIFlash::total_size() const
{
    if (!m_bd) return 0;
    return static_cast<mbed::BlockDevice*>(m_bd)->size();
}

uint32_t SPIFlash::block_size() const
{
    if (!m_bd) return 0;
    return static_cast<mbed::BlockDevice*>(m_bd)->get_erase_size();
}

uint32_t SPIFlash::page_size() const
{
    if (!m_bd) return 0;
    /* get_program_size() may return 1 (byte-level).
     * The actual write page is get_read_size() for this device. */
    return static_cast<mbed::BlockDevice*>(m_bd)->get_read_size();
}

/* ------------------------------------------------------------------ */
bool SPIFlash::erase_block(uint32_t addr)
{
    if (!m_ok) return false;
    auto* bd = static_cast<mbed::BlockDevice*>(m_bd);
    return bd->erase(addr, bd->get_erase_size()) == 0;
}

/* ------------------------------------------------------------------ */
bool SPIFlash::write_page(uint32_t addr, const uint8_t* data, size_t len)
{
    if (!m_ok) return false;
    auto* bd = static_cast<mbed::BlockDevice*>(m_bd);

    /* BlockDevice::program accepts arbitrary length within a page.
     * get_program_size() may return 1 (byte-level) but we can
     * write up to get_read_size() bytes at once. */
    return bd->program(data, addr, len) == 0;
}

/* ------------------------------------------------------------------ */
bool SPIFlash::read_data(uint32_t addr, uint8_t* buf, size_t len)
{
    if (!m_ok) return false;
    return static_cast<mbed::BlockDevice*>(m_bd)->read(buf, addr, len) == 0;
}

/* ------------------------------------------------------------------ */
bool SPIFlash::write_safe(uint32_t addr, const uint8_t* data, size_t len)
{
    if (!m_ok) return false;

    /* Read-modify-write: erase block, then write */
    auto* bd = static_cast<mbed::BlockDevice*>(m_bd);
    uint32_t bs = bd->get_erase_size();

    /* Align address down to block boundary */
    uint32_t block_addr = addr - (addr % bs);
    uint32_t offset = addr - block_addr;

    /* Read current block */
    uint8_t* block_buf = new uint8_t[bs];
    if (bd->read(block_buf, block_addr, bs) != 0) {
        delete[] block_buf;
        return false;
    }

    /* Modify */
    size_t copy_len = len;
    if (offset + len > bs) copy_len = bs - offset;
    memcpy(block_buf + offset, data, copy_len);

    /* Erase + write */
    if (bd->erase(block_addr, bs) != 0) {
        delete[] block_buf;
        return false;
    }

    int rc = bd->program(block_buf, block_addr, bs);
    delete[] block_buf;
    return rc == 0;
}

/* ------------------------------------------------------------------ */
bool SPIFlash::self_test()
{
    if (!m_ok) return false;

    const uint32_t TEST_ADDR = 0;
    const uint32_t TEST_LEN  = 256;

    Serial.print("SPI Flash self-test... ");

    /* 1. Erase */
    if (!erase_block(TEST_ADDR)) {
        Serial.println("erase FAILED");
        return false;
    }

    /* 2. Write pattern */
    uint8_t wr[256];
    for (int i = 0; i < 256; i++) wr[i] = (uint8_t)(i * 7 + 31);
    if (!write_page(TEST_ADDR, wr, TEST_LEN)) {
        Serial.println("write FAILED");
        return false;
    }

    /* 3. Read & verify */
    uint8_t rd[256];
    if (!read_data(TEST_ADDR, rd, TEST_LEN)) {
        Serial.println("read FAILED");
        return false;
    }

    for (int i = 0; i < 256; i++) {
        if (rd[i] != wr[i]) {
            Serial.print("MISMATCH at byte ");
            Serial.print(i);
            Serial.print(": w=0x"); Serial.print(wr[i], HEX);
            Serial.print(" r=0x"); Serial.println(rd[i], HEX);
            return false;
        }
    }

    Serial.println("ALL 256 BYTES MATCH ✅");
    return true;
}
