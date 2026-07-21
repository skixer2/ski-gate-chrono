/**
 * @file    spi_flash.cpp
 * @brief   MX25R1635F via mbed::BlockDevice. Phase 9 — silent.
 *          All output is emitted by the caller (main.cpp).
 */

#include "spi_flash.h"
#include <Arduino.h>
#include <BlockDevice.h>
#include "SPIFBlockDevice.h"

SPIFlash::SPIFlash() : m_bd(nullptr), m_ok(false) {}

bool SPIFlash::begin()
{
    /* V2.89: Release from DP FIRST — flash was put in deep power-down
       before previous reset to survive CS glitches. Safe even on
       cold boot (not in DP): 0xAB = Read Electronic ID in normal mode. */
    release_deep_powerdown();

    /* V2.29: Create explicit SPIFBlockDevice for MX25R1635F (SPI1:
     * p4 MOSI, p5 MISO, p3 SCK, p26 CS_FLASH). get_default_instance()
     * returns internal nRF flash (512KB) — writes beyond 512KB silently
     * truncate/overflow, corrupting the LittleFS superblock. */
    m_bd = new SPIFBlockDevice(p4, p5, p3, p26);
    if (!m_bd) return false;
    if (static_cast<mbed::BlockDevice*>(m_bd)->init() != 0) return false;
    m_ok = true;
    return self_test();
}

uint32_t SPIFlash::total_size() const {
    return m_bd ? static_cast<mbed::BlockDevice*>(m_bd)->size() : 0;
}
uint32_t SPIFlash::block_size() const {
    return m_bd ? static_cast<mbed::BlockDevice*>(m_bd)->get_erase_size() : 0;
}
uint32_t SPIFlash::page_size() const {
    return m_bd ? static_cast<mbed::BlockDevice*>(m_bd)->get_read_size() : 0;
}

bool SPIFlash::erase_block(uint32_t addr) {
    if (!m_ok) return false;
    auto* bd = static_cast<mbed::BlockDevice*>(m_bd);
    return bd->erase(addr, bd->get_erase_size()) == 0;
}

bool SPIFlash::write_page(uint32_t addr, const uint8_t* data, size_t len) {
    if (!m_ok) return false;
    return static_cast<mbed::BlockDevice*>(m_bd)->program(data, addr, len) == 0;
}

bool SPIFlash::read_data(uint32_t addr, uint8_t* buf, size_t len) {
    if (!m_ok) return false;
    return static_cast<mbed::BlockDevice*>(m_bd)->read(buf, addr, len) == 0;
}

bool SPIFlash::write_safe(uint32_t addr, const uint8_t* data, size_t len) {
    if (!m_ok) return false;
    auto* bd = static_cast<mbed::BlockDevice*>(m_bd);
    uint32_t bs = bd->get_erase_size();
    uint32_t block_addr = addr - (addr % bs);
    uint32_t offset = addr - block_addr;
    uint8_t* block_buf = new uint8_t[bs];
    if (bd->read(block_buf, block_addr, bs) != 0) { delete[] block_buf; return false; }
    size_t copy_len = len;
    if (offset + len > bs) copy_len = bs - offset;
    memcpy(block_buf + offset, data, copy_len);
    if (bd->erase(block_addr, bs) != 0) { delete[] block_buf; return false; }
    int rc = bd->program(block_buf, block_addr, bs);
    delete[] block_buf;
    return rc == 0;
}

bool SPIFlash::self_test()
{
    if (!m_ok) return false;
    const uint32_t TEST_ADDR = 0;
    const uint32_t TEST_LEN  = 256;
    if (!erase_block(TEST_ADDR)) return false;
    uint8_t wr[256];
    for (int i = 0; i < 256; i++) wr[i] = (uint8_t)(i * 7 + 31);
    if (!write_page(TEST_ADDR, wr, TEST_LEN)) return false;
    uint8_t rd[256];
    if (!read_data(TEST_ADDR, rd, TEST_LEN)) return false;
    for (int i = 0; i < 256; i++)
        if (rd[i] != wr[i]) return false;
    return true;
}

/* ==================================================================
 * V2.93: Deep Power-Down protects flash during NVIC_SystemReset()
 *
 * nRF52832 GPIOs float during reset → CS (P0.26) glitches LOW.
 * On Nicla dev board (no external CS pull-up), flash misinterprets
 * noise as SPI → possible corruption. DP minimises risk.
 *
 * On custom PCB WITH CS pull-up resistor: DP + metadata_sync()
 * before reset guarantees full data persistence through reboot.
 *
 * DP mode (0xB9) makes flash ignore ALL commands except 0xAB.
 * Release (0xAB) at boot wakes it. Safe on cold boot too:
 * 0xAB = Read Electronic ID in normal mode (harmless dummy read).
 * ================================================================== */

void SPIFlash::enter_deep_powerdown()
{
    /* V2.91: Simple bit-bang 0xB9 via Arduino digitalWrite.
       Same pattern as V2.82 that gave -22 (proved DP protects flash).
       Each bit takes ~2µs at mbed speed — well within MX25R timing. */
    pinMode(26, OUTPUT);
    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);
    digitalWrite(26, HIGH);  /* CS HIGH */
    digitalWrite(3, LOW);    /* SCK LOW */
    delayMicroseconds(10);

    /* CS LOW → select flash */
    digitalWrite(26, LOW);
    delayMicroseconds(1);

    /* Send 0xB9 (Enter Deep Power-Down), MSB first */
    for (int i = 7; i >= 0; i--) {
        digitalWrite(4, (0xB9 >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(3, HIGH);
        delayMicroseconds(1);
        digitalWrite(3, LOW);
    }

    delayMicroseconds(1);
    digitalWrite(26, HIGH);  /* CS HIGH → flash enters DP */
    delayMicroseconds(10);   /* tDP ≥ 3µs */
}

void SPIFlash::release_deep_powerdown()
{
    /* V2.91: Bit-bang 0xAB to release from DP at boot.
       In DP mode: wakes flash. In normal mode (cold boot):
       reads 1 byte Electronic ID — harmless dummy read. */
    pinMode(26, OUTPUT);
    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);
    digitalWrite(26, HIGH);  /* CS HIGH */
    digitalWrite(3, LOW);    /* SCK LOW */
    delayMicroseconds(10);

    /* CS LOW → select flash */
    digitalWrite(26, LOW);
    delayMicroseconds(1);

    /* Send 0xAB (Release DP / Read Electronic ID), MSB first */
    for (int i = 7; i >= 0; i--) {
        digitalWrite(4, (0xAB >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(3, HIGH);
        delayMicroseconds(1);
        digitalWrite(3, LOW);
    }

    delayMicroseconds(1);
    digitalWrite(26, HIGH);  /* CS↑ → flash exits DP */
    delayMicroseconds(50);   /* tDPDD = 35µs max */
}
