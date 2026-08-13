/**
 * @file    flash_layout.h
 * @brief   Size-aware SPI NOR map (v5.01).
 *
 * Pre-roll is FIXED (product arm window) — never grows with chip size:
 *   0x0000–0x13FFF   FlashRing 4000×20B (3000 ARM + 1000 drain)
 *
 * Top of chip (always last 4 × 4 KB sectors):
 *   chip-0x4000  Config
 *   chip-0x3000  Run index
 *   chip-0x2000  SPI self-test
 *   chip-0x1000  reserved
 *
 * Run slots: [0x14000 .. config) packed as N × SLOT_SIZE.
 * SLOT_SIZE matches the classic 2 MB map (~244 KB / slot) so a drop-in
 * MX25R3235F/6435F only gains more slots, not larger runs.
 *
 *   2 MB →  8 slots (identical addresses to v4.77–v5.00)
 *   4 MB → 16 slots
 *   8 MB → 32 slots (compile-time cap)
 */

#pragma once

#include <stdint.h>
#include "flash_ring.h"  /* RING_FLASH_END */

static constexpr uint32_t FLASH_SECTOR_SIZE     = 4096u;
static constexpr uint32_t FLASH_TOP_RESERVE     = 4u * FLASH_SECTOR_SIZE; /* config+index+test+spare */
static constexpr uint32_t FLASH_MIN_SIZE        = 2u * 1024u * 1024u;
static constexpr uint32_t FLASH_MAX_SIZE_3BYTE  = 16u * 1024u * 1024u; /* 3-byte addr ceiling */

/* Classic 2 MB slot geometry — keep for capacity/compat. */
static constexpr uint32_t RRS_DATA_BASE         = RING_FLASH_END; /* 0x14000 */
static constexpr uint32_t RRS_LEGACY_DATA_END   = 0x1FC000u;
static constexpr uint16_t RRS_LEGACY_SLOTS      = 8;
static constexpr uint32_t RRS_SLOT_SIZE         =
    ((RRS_LEGACY_DATA_END - RRS_DATA_BASE) / RRS_LEGACY_SLOTS / FLASH_SECTOR_SIZE)
    * FLASH_SECTOR_SIZE; /* 0x3D000 = 244 KiB */

/* BSS / index cap — enough for 8 MB @ ~244 KiB/slot. */
static constexpr uint16_t RRS_MAX_SLOTS_CAP     = 32;

struct FlashLayout {
    uint32_t chip_size;
    uint32_t config_addr;
    uint32_t index_addr;
    uint32_t selftest_addr;
    uint32_t data_base;     /* always RRS_DATA_BASE */
    uint32_t data_end;      /* exclusive — == config_addr */
    uint32_t slot_size;     /* RRS_SLOT_SIZE */
    uint16_t max_slots;     /* 8 / 16 / 32 … */
    bool     ok;
};

/** Compute layout from SFDP/total_size(). Safe to call more than once. */
bool flash_layout_init(uint32_t chip_size);

/** Last successful layout (ok=false until init). */
const FlashLayout& flash_layout();

/** Convenience accessors (0 / false if not inited). */
inline uint32_t flash_config_addr()   { return flash_layout().config_addr; }
inline uint32_t flash_index_addr()    { return flash_layout().index_addr; }
inline uint32_t flash_selftest_addr() { return flash_layout().selftest_addr; }
inline uint16_t flash_max_slots()     { return flash_layout().max_slots; }
inline uint32_t flash_slot_size()     { return flash_layout().slot_size; }
inline uint32_t flash_data_base()     { return flash_layout().data_base; }
inline uint32_t flash_slot_addr(uint16_t slot) {
    return flash_layout().data_base + (uint32_t)slot * flash_layout().slot_size;
}
