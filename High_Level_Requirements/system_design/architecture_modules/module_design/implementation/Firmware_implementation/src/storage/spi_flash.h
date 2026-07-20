/**
 * @file    spi_flash.h
 * @brief   MX25R1635F SPI NOR flash via SPIFBlockDevice.
 *          V2.29+: Uses explicit SPIFBlockDevice(p4,p5,p3,p26) on SPI1
 *          for the external 2MB flash.  mbed::BlockDevice::get_default_instance()
 *          returns internal nRF512 flash (512KB) — insufficient for run storage.
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

    /* V2.29: expose BD for LittleFS to share */
    void* get_bd() const { return m_bd; }

    /* V2.89: Deep Power-Down for NVIC_SystemReset() survival.
       Flash ignores all SPI in DP — CS glitches during reset are harmless.
       enter: before every reset. release: at boot before SPIFBlockDevice. */
    void enter_deep_powerdown();
    void release_deep_powerdown();

private:
    void* m_bd;    /* mbed::BlockDevice* */
    bool  m_ok;
};
