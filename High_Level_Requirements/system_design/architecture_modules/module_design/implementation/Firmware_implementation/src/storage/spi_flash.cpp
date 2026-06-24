/**
 * @file    spi_flash.cpp
 * @brief   MX25R1635F via mbed::BlockDevice. Phase 9 — silent.
 *          All output is emitted by the caller (main.cpp).
 */

#include "spi_flash.h"
#include <Arduino.h>
#include <BlockDevice.h>

SPIFlash::SPIFlash() : m_bd(nullptr), m_ok(false) {}

bool SPIFlash::begin()
{
    m_bd = mbed::BlockDevice::get_default_instance();
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
