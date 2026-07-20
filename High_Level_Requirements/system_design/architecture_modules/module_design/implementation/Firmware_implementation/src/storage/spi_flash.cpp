/**
 * @file    spi_flash.cpp
 * @brief   MX25R1635F via mbed::BlockDevice. Phase 9 — silent.
 *          All output is emitted by the caller (main.cpp).
 */

#include "spi_flash.h"
#include <Arduino.h>
#include <BlockDevice.h>
#include "SPIFBlockDevice.h"
#include "nrf_gpio.h"

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
 * V2.89: Deep Power-Down for NVIC_SystemReset() survival
 *
 * nRF52832 GPIOs float during reset → CS glitches LOW → flash
 * interprets noise as SPI command → superblock corrupted → -138.
 *
 * DP mode (0xB9) makes flash ignore ALL commands except 0xAB.
 * Release (0xAB) at boot wakes it. Safe on cold boot too:
 * 0xAB = Read Electronic ID in normal mode (harmless dummy read).
 *
 * Uses nRF52 HW SPI1 (same peripheral as SPIFBlockDevice).
 * ================================================================== */

void SPIFlash::enter_deep_powerdown()
{
    /* Drive CS HIGH before touching SPI to prevent glitch. */
    nrf_gpio_cfg_output(26);
    nrf_gpio_pin_set(26);

    /* Configure SPI1 manually — same pins as SPIFBlockDevice:
       SCK=p3, MOSI=p4, MISO=p5. 1 MHz well within MX25R spec. */
    NRF_SPI1->PSELSCK  = 3;
    NRF_SPI1->PSELMOSI = 4;
    NRF_SPI1->PSELMISO = 5;
    NRF_SPI1->FREQUENCY = 0x04000000;  /* 1 MHz */
    NRF_SPI1->CONFIG = 0;              /* Mode 0, MSB first */
    NRF_SPI1->EVENTS_READY = 0;
    NRF_SPI1->ENABLE = 1;

    /* CS LOW → select flash */
    nrf_gpio_pin_clear(26);

    /* Send 0xB9 (Enter Deep Power-Down) */
    NRF_SPI1->TXD = 0xB9;
    while (!NRF_SPI1->EVENTS_READY) {}
    NRF_SPI1->EVENTS_READY = 0;
    (void)NRF_SPI1->RXD;  /* dummy read to clear RX */

    /* CS HIGH → flash enters DP */
    nrf_gpio_pin_set(26);

    /* ~50 µs for DP entry (spec: tDP = 3µs min) */
    for (volatile int i = 0; i < 1000; i++) {}

    /* Disable SPI1 and disconnect pins so SPIFBlockDevice can
       reconfigure them cleanly on next boot. */
    NRF_SPI1->ENABLE = 0;
    NRF_SPI1->PSELSCK  = 0xFFFFFFFF;
    NRF_SPI1->PSELMOSI = 0xFFFFFFFF;
    NRF_SPI1->PSELMISO = 0xFFFFFFFF;

    /* Leave CS as input for mbed to reclaim */
    nrf_gpio_cfg_input(26, NRF_GPIO_PIN_NOPULL);
}

void SPIFlash::release_deep_powerdown()
{
    /* Drive CS HIGH first */
    nrf_gpio_cfg_output(26);
    nrf_gpio_pin_set(26);

    /* Configure SPI1 — same as DP entry */
    NRF_SPI1->PSELSCK  = 3;
    NRF_SPI1->PSELMOSI = 4;
    NRF_SPI1->PSELMISO = 5;
    NRF_SPI1->FREQUENCY = 0x04000000;
    NRF_SPI1->CONFIG = 0;
    NRF_SPI1->EVENTS_READY = 0;
    NRF_SPI1->ENABLE = 1;

    /* CS LOW → select flash */
    nrf_gpio_pin_clear(26);

    /* Send 0xAB (Release from DP / Read Electronic ID).
       In DP mode: wakes flash. In normal mode: reads 1 byte ID. */
    NRF_SPI1->TXD = 0xAB;
    while (!NRF_SPI1->EVENTS_READY) {}
    NRF_SPI1->EVENTS_READY = 0;
    (void)NRF_SPI1->RXD;  /* dummy read — discards ID byte */

    /* CS HIGH → flash exits DP (or completes dummy read) */
    nrf_gpio_pin_set(26);

    /* tDPDD = 35µs max from CS↑ to ready */
    for (volatile int i = 0; i < 1000; i++) {}

    /* Disable SPI1 and disconnect pins */
    NRF_SPI1->ENABLE = 0;
    NRF_SPI1->PSELSCK  = 0xFFFFFFFF;
    NRF_SPI1->PSELMOSI = 0xFFFFFFFF;
    NRF_SPI1->PSELMISO = 0xFFFFFFFF;

    /* Leave CS as input */
    nrf_gpio_cfg_input(26, NRF_GPIO_PIN_NOPULL);
}
