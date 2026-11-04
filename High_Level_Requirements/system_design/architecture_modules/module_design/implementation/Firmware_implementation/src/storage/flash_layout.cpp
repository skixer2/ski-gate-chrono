/**
 * @file    flash_layout.cpp
 * @brief   SFDP-size → run-slot map (pre-roll fixed).
 */

#include "flash_layout.h"
#include <Arduino.h>
#include <string.h>

static FlashLayout g_layout = {};

bool flash_layout_init(uint32_t chip_size)
{
    FlashLayout L;
    memset(&L, 0, sizeof(L));

    /* Clamp / default: unknown or tiny → classic 2 MB map. */
    if (chip_size < FLASH_MIN_SIZE)
        chip_size = FLASH_MIN_SIZE;
    if (chip_size > FLASH_MAX_SIZE_3BYTE)
        chip_size = FLASH_MAX_SIZE_3BYTE;

    /* Round down to sector so top-of-chip math is clean. */
    chip_size = (chip_size / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

    L.chip_size     = chip_size;
    L.config_addr   = chip_size - 4u * FLASH_SECTOR_SIZE; /* …C000 on 2 MB */
    L.index_addr    = chip_size - 3u * FLASH_SECTOR_SIZE; /* …D000 */
    L.selftest_addr = chip_size - 2u * FLASH_SECTOR_SIZE; /* …E000 */
    L.data_base     = RRS_DATA_BASE;
    L.data_end      = L.config_addr;
    L.slot_size     = RRS_SLOT_SIZE;

    if (L.data_end <= L.data_base + L.slot_size) {
        L.ok = false;
        g_layout = L;
        return false;
    }

    uint32_t span = L.data_end - L.data_base;
    uint16_t n = (uint16_t)(span / L.slot_size);
    if (n < 1) n = 1;
    if (n > RRS_MAX_SLOTS_CAP) n = RRS_MAX_SLOTS_CAP;
    /* Prefer even packing; leave unused tail before config if any. */
    L.max_slots = n;
    L.ok = true;
    g_layout = L;

    Serial.print("{\"ev\":\"flash_map\",\"size_kb\":");
    Serial.print((long)(chip_size / 1024));
    Serial.print(",\"slots\":");
    Serial.print((int)L.max_slots);
    Serial.print(",\"slot_kb\":");
    Serial.print((long)(L.slot_size / 1024));
    Serial.print(",\"cfg\":");
    Serial.print((long)L.config_addr);
    Serial.print(",\"idx\":");
    Serial.print((long)L.index_addr);
    Serial.print(",\"preroll_end\":");
    Serial.print((long)RING_FLASH_END);
    Serial.println("}");
    return true;
}

const FlashLayout& flash_layout()
{
    return g_layout;
}
