/**
 * @file    flash_manager.cpp
 * @brief   Circular flash storage manager — implementation.
 *
 * Implements sgc_architecture_devices.md §4 and sgc_system_design.md §5.
 *
 * Never prints to Serial — all output is emitted by the caller (main.cpp).
 */

#include "flash_manager.h"
#include "spi_flash.h"
#include <Arduino.h>
#include <cstring>

/* ── CRC32 lookup table (Ethernet / PKZIP polynomial) ─────────── */
static const uint32_t CRC32_TABLE[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,
    0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,
    0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,0x1DB71064,0x6AB020F2,
    0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,
    0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,
    0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,
    0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,
    0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,
    0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,
    0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,
    0x91646C97,0xE6635C01,0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,
    0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,0x65B0D9C6,0x12B7E950,
    0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,
    0xA4D1C46D,0xD3D6F4FB,0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,
    0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,0x5005713C,0x270241AA,
    0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,
    0xB7BD5C3B,0xC0BA6CAD,0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,
    0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,0xE3630B12,0x94643B84,
    0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,
    0x196C3671,0x6E6B06E7,0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,
    0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,0xD6D6A3E8,0xA1D1937E,
    0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,
    0x316E8EEF,0x4669BE79,0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,
    0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,0xC5BA3BBE,0xB2BD0B28,
    0x2BB45A92,0x5CB30A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
    0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,
    0x72076785,0x05005713,0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,
    0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,0x86D3D2D4,0xF1D4E242,
    0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
    0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,
    0x616BFFD3,0x166CCF45,0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,
    0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,0xAED16A4A,0xD9D65ADC,
    0x40BF0B66,0x37B83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
    0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,
    0x54DE5729,0x23D967BF,0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,
    0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D
};

/* ── CRC32 helpers ────────────────────────────────────────────── */

uint32_t FlashManager::crc32_update(uint32_t crc, uint8_t byte) {
    return (crc >> 8) ^ CRC32_TABLE[(crc ^ byte) & 0xFF];
}

uint32_t FlashManager::crc32_buffer(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32_update(crc, data[i]);
    return crc ^ 0xFFFFFFFF;
}

/* ── Construction ─────────────────────────────────────────────── */

FlashManager::FlashManager(SPIFlash& flash)
    : m_flash(flash)
    , m_entry_count(0)
    , m_index_valid(false)
    , m_last_erased_sector(UINT32_MAX)
{
    memset(&m_index, 0, sizeof(m_index));
    memset(m_entries, 0, sizeof(m_entries));
}

/* ── Address math ─────────────────────────────────────────────── */

uint32_t FlashManager::wrap_address(uint32_t addr) const
{
    if (addr < RUN_DATA_START) return RUN_DATA_START;
    while (addr >= RUN_DATA_START + RUN_DATA_SIZE)
        addr -= RUN_DATA_SIZE;
    return addr;
}

uint32_t FlashManager::sector_of(uint32_t addr) const
{
    return addr / FLASH_SECTOR_SIZE;
}

/* ── Index I/O ────────────────────────────────────────────────── */

void FlashManager::read_index()
{
    FlashIndex raw;
    m_flash.read_data(INDEX_ADDR, (uint8_t*)&raw, sizeof(raw));

    if (raw.magic != 0x53474300 || raw.entry_count > MAX_ENTRIES) {
        m_index_valid = false;
        return;
    }

    /* Read entries */
    size_t entry_bytes = raw.entry_count * sizeof(RunEntry);
    if (entry_bytes > 0) {
        m_flash.read_data(INDEX_ADDR + sizeof(FlashIndex),
                          (uint8_t*)m_entries, entry_bytes);
    }

    m_index = raw;
    m_entry_count = raw.entry_count;

    /* Sanity-check read_head and write_head */
    if (m_index.read_head != 0 &&
        (m_index.read_head < RUN_DATA_START ||
         m_index.read_head >= RUN_DATA_START + RUN_DATA_SIZE)) {
        m_index_valid = false;
        return;
    }
    if (m_index.write_head < RUN_DATA_START ||
        m_index.write_head >= RUN_DATA_START + RUN_DATA_SIZE) {
        m_index_valid = false;
        return;
    }

    m_index_valid = true;
}

void FlashManager::write_index()
{
    /* Erase both index sectors */
    m_flash.erase_block(INDEX_ADDR);
    m_flash.erase_block(INDEX_ADDR + FLASH_SECTOR_SIZE);

    m_index.magic = 0x53474300;
    m_index.entry_count = m_entry_count;

    m_flash.write_page(INDEX_ADDR, (const uint8_t*)&m_index, sizeof(m_index));

    if (m_entry_count > 0) {
        size_t entry_bytes = m_entry_count * sizeof(RunEntry);
        m_flash.write_page(INDEX_ADDR + sizeof(FlashIndex),
                           (const uint8_t*)m_entries, entry_bytes);
    }
}

/* ── Recovery ─────────────────────────────────────────────────── */

/**
 * Scan all run data for valid run headers and rebuild the index.
 * O(n) in number of sectors, worst case < 100 ms for full flash scan.
 */
bool FlashManager::recover_index()
{
    memset(&m_index, 0, sizeof(m_index));
    memset(m_entries, 0, sizeof(m_entries));
    m_entry_count = 0;

    uint32_t addr = RUN_DATA_START;
    uint32_t end_addr = RUN_DATA_START + RUN_DATA_SIZE;
    uint32_t first_valid = 0;
    uint32_t last_valid_end = RUN_DATA_START;
    uint16_t max_id = 0;

    while (addr < end_addr) {
        RunHeader hdr;
        if (!m_flash.read_data(addr, (uint8_t*)&hdr, sizeof(hdr))) {
            addr += FLASH_SECTOR_SIZE;
            continue;
        }

        /* Sanity check: format_ver 1-3, arm_side 0-1, data_size reasonable */
        if (hdr.format_ver < 1 || hdr.format_ver > 3 ||
            hdr.arm_side > 1 ||
            hdr.data_size > 0x200000 ||
            hdr.data_size == 0) {
            addr += FLASH_SECTOR_SIZE;
            continue;
        }

        uint32_t run_end = addr + sizeof(RunHeader) + hdr.data_size + CRC32_TRAILER_SIZE;
        /* Align to sector */
        run_end = ((run_end + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

        if (run_end > end_addr) {
            /* Run would extend past end of data area — wraps around.
               This is valid in a circular buffer: treat as end of data. */
            run_end = end_addr;
        }

        if (!first_valid) first_valid = addr;

        /* Assign a run_id. If we can find one in the header (not directly stored),
           use a reconstructed counter. We'll use the monotonic counter approach:
           the number of valid runs found gives the run_id sequence. */
        uint16_t assigned_id = m_entry_count;

        /* Check if this looks like a valid run (not erased flash = 0xFF) */
        if (hdr.format_ver == 0xFF) {
            addr += FLASH_SECTOR_SIZE;
            continue;
        }

        RunEntry& entry = m_entries[m_entry_count];
        entry.run_id = assigned_id;
        entry.page_start = addr;
        entry.page_end = run_end;
        entry.timestamp = hdr.ts_utc;
        entry.arm_side = hdr.arm_side;
        entry.format_version = hdr.format_ver;
        entry.compressed_size = hdr.data_size;
        entry.frame_count = hdr.frame_count;
        memset(entry._reserved, 0, sizeof(entry._reserved));

        m_entry_count++;
        max_id = assigned_id;
        last_valid_end = run_end;
        addr = run_end;

        if (m_entry_count >= MAX_ENTRIES) break;
    }

    if (m_entry_count == 0) {
        /* Fresh flash — no runs found */
        m_index.magic = 0x53474300;
        m_index.run_count = 0;
        m_index.read_head = 0;  /* 0 = empty */
        m_index.write_head = RUN_DATA_START;
        m_index.write_counter = 0;
        m_index.entry_count = 0;
        write_index();
        m_index_valid = true;
        return false;
    }

    /* Build index from recovered entries */
    m_index.magic = 0x53474300;
    m_index.run_count = max_id + 1;
    m_index.read_head = first_valid;
    m_index.write_head = wrap_address(last_valid_end);
    m_index.write_counter = max_id + 1;
    m_index.entry_count = m_entry_count;
    write_index();
    m_index_valid = true;
    return true;
}

/* ── begin() ──────────────────────────────────────────────────── */

bool FlashManager::begin()
{
    read_index();
    if (!m_index_valid) {
        /* Index corrupted or missing — scan headers to rebuild */
        recover_index();
    }
    return m_index_valid;
}

/* ── Sector erase management ──────────────────────────────────── */

bool FlashManager::ensure_erased(uint32_t addr)
{
    uint32_t sec = sector_of(addr);
    if (sec == m_last_erased_sector) return true;

    if (!m_flash.erase_block(addr)) return false;
    m_last_erased_sector = sec;
    return true;
}

/* ── create_run() ─────────────────────────────────────────────── */

uint32_t FlashManager::create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy)
{
    /* ── Estimate run size (worst case: 2-min DH run ≈ 108 KB) ── */
    uint32_t estimated_bytes = 110000; /* generous estimate */

    /* ── Check for overlap with read_head (circular) ── */
    if (m_index.read_head != 0) {
        /* Is there enough free space? */
        uint32_t free_start = m_index.write_head;
        uint32_t free_end   = m_index.read_head;
        uint32_t free_space;

        if (free_start < free_end) {
            /* Normal: write_head before read_head */
            free_space = free_end - free_start;
        } else {
            /* Wrapped: write_head past read_head (circular gap) */
            free_space = (RUN_DATA_START + RUN_DATA_SIZE - free_start) + (free_end - RUN_DATA_START);
        }

        while (free_space < estimated_bytes && m_entry_count > 0) {
            /* Delete oldest run (by flash position = first entry) */
            const RunEntry& oldest = m_entries[0];
            m_index.read_head = oldest.page_end;

            /* Remove from entry table */
            remove_entry(0);

            /* Recalculate free space */
            free_start = m_index.write_head;
            free_end = m_index.read_head;
            if (free_start < free_end) {
                free_space = free_end - free_start;
            } else {
                free_space = (RUN_DATA_START + RUN_DATA_SIZE - free_start)
                           + (free_end - RUN_DATA_START);
            }
        }
    }

    if (m_entry_count >= MAX_ENTRIES) {
        /* Table full — delete oldest to make room */
        remove_entry(0);
        if (m_entry_count > 0) {
            m_index.read_head = m_entries[0].page_start;
        } else {
            m_index.read_head = 0;
        }
    }

    /* ── Erase target sector ── */
    uint32_t write_addr = m_index.write_head;
    if (!ensure_erased(write_addr)) return 0;

    /* ── Write RunHeader ── */
    RunHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.format_ver = 2;
    hdr.arm_side = arm_side;
    hdr.ts_utc = 0;  /* Will be set by time sync, or left 0 */
    hdr.baro_temp = baro_temp;
    hdr.data_size = 0;  /* Filled in by close_run() */
    hdr.frame_count = 0;
    hdr.cal_accuracy = cal_accuracy;

    if (!m_flash.write_page(write_addr, (const uint8_t*)&hdr, sizeof(hdr))) {
        return 0;
    }

    /* Return address where compressed frames should be written */
    return write_addr + sizeof(RunHeader);
}

/* ── close_run() ──────────────────────────────────────────────── */

uint16_t FlashManager::close_run(uint32_t run_start, uint32_t compressed_size, uint32_t frame_count)
{
    uint16_t run_id = m_index.run_count;

    /* ── Update the data_size and frame_count fields in the RunHeader ── */
    RunHeader hdr;
    m_flash.read_data(run_start, (uint8_t*)&hdr, sizeof(hdr));
    hdr.data_size = compressed_size;
    hdr.frame_count = (uint16_t)frame_count;
    m_flash.write_page(run_start, (const uint8_t*)&hdr, sizeof(hdr));

    /* ── Compute CRC32 of compressed data ── */
    uint32_t data_addr = run_start + sizeof(RunHeader);
    uint32_t crc = 0xFFFFFFFF;

    /* Read in chunks to avoid large buffer */
    uint8_t chunk[256];
    uint32_t remaining = compressed_size;
    uint32_t addr = data_addr;

    while (remaining > 0) {
        size_t chunk_len = (remaining > sizeof(chunk)) ? sizeof(chunk) : remaining;
        m_flash.read_data(addr, chunk, chunk_len);
        for (size_t i = 0; i < chunk_len; i++) {
            crc = crc32_update(crc, chunk[i]);
        }
        addr += chunk_len;
        remaining -= chunk_len;
    }
    crc ^= 0xFFFFFFFF;

    /* ── Write CRC32 trailer ── */
    uint32_t trailer_addr = data_addr + compressed_size;
    uint8_t trailer[6];
    trailer[0] = CRC32_MAGIC_HI;
    trailer[1] = CRC32_MAGIC_LO;
    trailer[2] = (uint8_t)(crc & 0xFF);
    trailer[3] = (uint8_t)((crc >> 8) & 0xFF);
    trailer[4] = (uint8_t)((crc >> 16) & 0xFF);
    trailer[5] = (uint8_t)((crc >> 24) & 0xFF);

    /* Write trailer — may cross a page boundary */
    m_flash.write_page(trailer_addr, trailer, sizeof(trailer));

    /* ── Advance write_head (sector-aligned) ── */
    m_index.write_head = advance_write_head(run_start, compressed_size);

    /* ── Add entry to the table ── */
    RunEntry& entry = m_entries[m_entry_count];
    memset(&entry, 0, sizeof(entry));
    entry.run_id = run_id;
    entry.page_start = run_start;
    entry.page_end = m_index.write_head;
    entry.timestamp = hdr.ts_utc;
    entry.arm_side = hdr.arm_side;
    entry.format_version = hdr.format_ver;
    entry.compressed_size = compressed_size;
    entry.frame_count = frame_count;

    m_entry_count++;

    /* Set read_head if this is the first run */
    if (m_index.read_head == 0) {
        m_index.read_head = run_start;
    }

    /* ── Update index ── */
    m_index.run_count++;
    m_index.write_counter++;
    write_index();

    return run_id;
}

uint32_t FlashManager::advance_write_head(uint32_t run_start, uint32_t compressed_size)
{
    uint32_t run_end = run_start + sizeof(RunHeader) + compressed_size + CRC32_TRAILER_SIZE;

    /* Align to next sector boundary */
    uint32_t next = ((run_end + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

    /* Wrap within run data area */
    return wrap_address(next);
}

/* ── Queries ──────────────────────────────────────────────────── */

uint8_t FlashManager::flash_used_pct() const
{
    if (m_entry_count == 0) return 0;

    uint32_t used = 0;
    for (uint16_t i = 0; i < m_entry_count; i++) {
        uint32_t start = m_entries[i].page_start;
        uint32_t end   = m_entries[i].page_end;
        if (end > start) {
            used += (end - start);
        }
    }

    uint64_t pct = ((uint64_t)used * 100) / RUN_DATA_SIZE;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

uint32_t FlashManager::oldest_run_age() const
{
    if (m_entry_count == 0) return 0;
    return m_entries[0].timestamp;
}

const RunEntry* FlashManager::get_entry(uint16_t idx) const
{
    if (idx >= m_entry_count) return nullptr;
    return &m_entries[idx];
}

/* ── Entry management ─────────────────────────────────────────── */

int FlashManager::find_entry_by_id(uint16_t run_id) const
{
    for (uint16_t i = 0; i < m_entry_count; i++) {
        if (m_entries[i].run_id == run_id) return (int)i;
    }
    return -1;
}

void FlashManager::remove_entry(int idx)
{
    if (idx < 0 || idx >= (int)m_entry_count) return;
    /* Shift remaining entries down */
    for (uint16_t i = (uint16_t)idx; i < m_entry_count - 1; i++) {
        m_entries[i] = m_entries[i + 1];
    }
    m_entry_count--;
    memset(&m_entries[m_entry_count], 0, sizeof(RunEntry));
}

/* ── Data I/O ─────────────────────────────────────────────────── */

bool FlashManager::read_run_header(uint32_t addr, RunHeader& hdr) const
{
    if (addr < RUN_DATA_START || addr >= RUN_DATA_START + RUN_DATA_SIZE)
        return false;
    return m_flash.read_data(addr, (uint8_t*)&hdr, sizeof(hdr));
}

bool FlashManager::read_data(uint32_t addr, uint8_t* buf, size_t len) const
{
    return m_flash.read_data(addr, buf, len);
}

bool FlashManager::write_data(uint32_t addr, const uint8_t* data, size_t len)
{
    if (!ensure_erased(addr)) return false;
    return m_flash.write_page(addr, data, len);
}

/* ── Run list (JSON) ──────────────────────────────────────────── */

const char* FlashManager::build_run_list(char* buf, size_t buf_size) const
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");

    bool first = true;
    for (uint16_t i = 0; i < m_entry_count; i++) {
        const RunEntry& e = m_entries[i];
        if (!first) pos += snprintf(buf + pos, buf_size - pos, ",");
        first = false;
        pos += snprintf(buf + pos, buf_size - pos,
            "{\"id\":%u,\"ts\":%lu,\"size\":%lu,\"side\":\"%s\"}",
            e.run_id,
            (unsigned long)e.timestamp,
            (unsigned long)e.compressed_size,
            e.arm_side ? "right" : "left");

        if ((size_t)pos >= buf_size - 2) break; /* safety truncation */
    }

    pos += snprintf(buf + pos, buf_size - pos, "]");
    return buf;
}

/* ── Factory reset ────────────────────────────────────────────── */

void FlashManager::erase_all()
{
    /* Erase all run data sectors (4 through 509) */
    for (uint32_t addr = RUN_DATA_START; addr < RUN_DATA_START + RUN_DATA_SIZE; addr += FLASH_SECTOR_SIZE) {
        m_flash.erase_block(addr);
    }

    /* Reset index */
    memset(&m_index, 0, sizeof(m_index));
    memset(m_entries, 0, sizeof(m_entries));
    m_entry_count = 0;

    m_index.magic = 0x53474300;
    m_index.run_count = 0;
    m_index.read_head = 0;
    m_index.write_head = RUN_DATA_START;
    m_index.write_counter = 0;
    m_index.entry_count = 0;
    write_index();

    m_index_valid = true;
    m_last_erased_sector = UINT32_MAX;
}
