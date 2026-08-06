/**
 * @file    raw_run_writer.cpp
 * @brief   Pre-erased raw SPI run writer — Opt A S04 spike (v4.62).
 */

#include "raw_run_writer.h"
#include "spi_flash.h"
#include <Arduino.h>

RawRunWriter::RawRunWriter()
    : m_flash(nullptr), m_active(false),
      m_addr(RAW_RUN_BASE), m_bytes(0), m_write_err(0)
{
}

bool RawRunWriter::begin(SPIFlash& flash)
{
    m_flash = &flash;
    m_active = false;
    m_addr = RAW_RUN_BASE;
    m_bytes = 0;
    m_write_err = 0;

    /* Pre-erase entire 48 KB slab once. 12 × 4 KB sector erase.
       Paid at LOGGING entry (force-l / future ARM), never mid-frame. */
    for (uint32_t a = RAW_RUN_BASE; a < RAW_RUN_END; a += RAW_RUN_SECTOR) {
        if (!flash.erase_block(a)) {
            m_write_err++;
            Serial.print("{\"ev\":\"raw_erase_err\",\"addr\":");
            Serial.print(a);
            Serial.println("}");
            return false;
        }
    }

    m_active = true;
    Serial.print("{\"ev\":\"raw_run_begin\",\"base\":");
    Serial.print(RAW_RUN_BASE);
    Serial.print(",\"sz\":");
    Serial.print(RAW_RUN_SIZE);
    Serial.println("}");
    return true;
}

bool RawRunWriter::program(const uint8_t* data, size_t len)
{
    if (!m_active || !m_flash || !data || len == 0) return false;
    if (m_addr + len > RAW_RUN_END) {
        m_write_err++;
        return false;  /* slab full — S04 20s @ ~10 B/f << 48 KB */
    }

    /* MX25R page = 256 B. Split on page boundaries for SPIFBlockDevice. */
    size_t off = 0;
    while (off < len) {
        uint32_t page_off = m_addr & 0xFFu;
        size_t space = 256u - (size_t)page_off;
        size_t chunk = len - off;
        if (chunk > space) chunk = space;

        if (!m_flash->write_page(m_addr, data + off, chunk)) {
            m_write_err++;
            return false;
        }
        m_addr += (uint32_t)chunk;
        m_bytes += (uint32_t)chunk;
        off += chunk;
    }
    return true;
}

uint32_t RawRunWriter::close()
{
    m_active = false;
    return m_bytes;
}
