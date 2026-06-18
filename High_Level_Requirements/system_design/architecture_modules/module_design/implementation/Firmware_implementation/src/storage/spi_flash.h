/**
 * @file    spi_flash.h
 * @brief   MX25R1635F SPI NOR flash via mbed BlockDevice API.
 *          Uses mbed::BlockDevice::get_default_instance() — the canonical
 *          Nicla Sense ME approach (pre-configured for SPI1: p4/p5/p3/p26).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

class SPIFlash
{
public:
    SPIFlash();

    bool begin();

    /* Erase a block (4KB on MX25R1635F) */
    bool erase_block(uint32_t addr);

    /* Write up to one page (256 bytes). Must erase block first. */
    bool write_page(uint32_t addr, const uint8_t* data, size_t len);

    /* Read arbitrary bytes */
    bool read_data(uint32_t addr, uint8_t* buf, size_t len);

    /* Safe write: auto-erases block if needed, handles page boundaries */
    bool write_safe(uint32_t addr, const uint8_t* data, size_t len);

    /* Utility */
    uint32_t total_size() const;
    uint32_t block_size() const;
    uint32_t page_size()  const;

    /* Self-test in sector 0 */
    bool self_test();

private:
    void* m_bd;    /* mbed::BlockDevice* */
    bool  m_ok;
};
