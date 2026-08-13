/**
 * @file    raw_run_store.cpp
 * @brief   Multi-slot pre-erased raw run storage (Opt A, v4.63/v5.01 size-aware).
 *          prepare_next_run(): full-slot erase at POST_RUN/boot (not ARM).
 */

#include "raw_run_store.h"
#include "spi_flash.h"
#include "flash_layout.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

/* CRC32 identical table to prior LittleFSStorage (BLE/phone compatible). */
uint32_t RawRunStore::crc32_update(uint32_t crc, uint8_t byte)
{
    static const uint32_t table[256] = {
        0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
        0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
        0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
        0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
        0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
        0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
        0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
        0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
        0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
        0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
        0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
        0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
        0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
        0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
        0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
        0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
        0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
        0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
        0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
        0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
        0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
        0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
        0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
        0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
        0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
        0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
        0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
        0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
        0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
        0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
        0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
        0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
    };
    return table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
}

uint32_t RawRunStore::crc32_buffer(const uint8_t* data, size_t len)
{
    uint32_t crc = crc32_initial();
    for (size_t i = 0; i < len; i++) crc = crc32_update(crc, data[i]);
    return crc32_finalize(crc);
}

RawRunStore::RawRunStore()
    : m_flash(nullptr), m_ok(false), m_writing(false), m_prepared(false),
      m_prep_slot(0), m_write_slot(0), m_write_run_id(0),
      m_slot_base(0), m_slot_end(0), m_cursor(0), m_erased_end(0),
      m_payload_bytes(0), m_run_crc(0xFFFFFFFFu),
      m_write_err(0), m_pending_arm_side(0), m_pending_baro_temp(0),
      m_pending_cal(0), m_pending_ts_utc(0), m_next_run_id(0), m_entry_count(0),
      m_slot_used_mask(0), m_max_slots(RRS_LEGACY_SLOTS), m_slot_size(RRS_SLOT_SIZE),
      m_index_addr(0x1FD000u)
{
    memset(m_entries, 0, sizeof(m_entries));
    memset(m_entry_slot, 0, sizeof(m_entry_slot));
}

bool RawRunStore::begin(SPIFlash& flash)
{
    m_flash = &flash;
    m_ok = (m_flash != nullptr);
    m_writing = false;
    m_prepared = false;
    if (!m_ok) return false;

    /* V5.01: layout already built in SPIFlash::begin(); just bind. */
    const FlashLayout& L = flash_layout();
    if (!L.ok) {
        /* Defensive re-init if begin order ever changes. */
        if (!flash_layout_init(m_flash->total_size()) || !flash_layout().ok) {
            Serial.println("{\"ev\":\"raw_store\",\"ok\":0,\"reason\":\"layout\"}");
            m_ok = false;
            return false;
        }
    }
    const FlashLayout& L2 = flash_layout();
    m_max_slots  = L2.max_slots;
    m_slot_size  = L2.slot_size;
    m_index_addr = L2.index_addr;

    if (!load_index()) {
        m_next_run_id = 0;
        m_entry_count = 0;
        m_slot_used_mask = 0;
        memset(m_entries, 0, sizeof(m_entries));
        persist_index();
    }
    Serial.print("{\"ev\":\"raw_store\",\"ok\":1,\"slots\":");
    Serial.print((int)m_max_slots);
    Serial.print(",\"slot_kb\":");
    Serial.print((long)(m_slot_size / 1024));
    Serial.print(",\"chip_kb\":");
    Serial.print((long)(L.chip_size / 1024));
    Serial.print(",\"runs\":");
    Serial.print((int)m_entry_count);
    Serial.println("}");
    return true;
}

bool RawRunStore::load_index()
{
    if (!m_flash) return false;

    /* Header first — decide RRS2 vs legacy RRS1. */
    uint8_t hdrbuf[sizeof(IndexHeader)];
    if (!m_flash->read_data(m_index_addr, hdrbuf, sizeof(hdrbuf))) return false;

    uint32_t magic = 0;
    memcpy(&magic, hdrbuf, 4);

    if (magic == INDEX_MAGIC) {
        IndexHeader ih;
        memcpy(&ih, hdrbuf, sizeof(ih));
        if (ih.entry_count > m_max_slots || ih.entry_count > RRS_MAX_SLOTS_CAP)
            return false;
        /* Geometry mismatch (e.g. index from larger chip) — reject, re-init. */
        if (ih.max_slots != 0 && ih.max_slots != m_max_slots)
            return false;
        m_next_run_id = ih.next_run_id;
        m_entry_count = ih.entry_count;
        m_slot_used_mask = ih.slot_used_mask;

        /* Entries: each RunEntry + 1B slot id. May span multiple pages. */
        size_t need = (size_t)m_entry_count * (sizeof(RunEntry) + 1);
        if (need == 0) return true;
        /* Read up to one full sector of payload after header (index fits 4 KB). */
        uint8_t body[FLASH_SECTOR_SIZE - sizeof(IndexHeader)];
        size_t body_max = sizeof(body);
        if (need > body_max) return false;
        if (!m_flash->read_data(m_index_addr + sizeof(IndexHeader), body, need))
            return false;
        const uint8_t* p = body;
        for (uint16_t i = 0; i < m_entry_count; i++) {
            memcpy(&m_entries[i], p, sizeof(RunEntry));
            p += sizeof(RunEntry);
            m_entry_slot[i] = *p++;
            if (m_entry_slot[i] >= m_max_slots) return false;
        }
        return true;
    }

    /* Legacy RRS1 (v4.63–v5.00): 8-bit mask, no max_slots field. */
    if (magic == INDEX_MAGIC_LEGACY) {
        struct __attribute__((packed)) LegacyHdr {
            uint32_t magic;
            uint16_t next_run_id;
            uint16_t entry_count;
            uint8_t  slot_used_mask;
            uint8_t  _pad[7];
        };
        LegacyHdr lh;
        memcpy(&lh, hdrbuf, sizeof(lh));
        if (lh.entry_count > m_max_slots || lh.entry_count > 8) return false;
        m_next_run_id = lh.next_run_id;
        m_entry_count = lh.entry_count;
        m_slot_used_mask = lh.slot_used_mask;

        size_t need = (size_t)m_entry_count * (sizeof(RunEntry) + 1);
        uint8_t body[256];
        if (need > sizeof(body)) return false;
        if (need > 0) {
            if (!m_flash->read_data(m_index_addr + sizeof(LegacyHdr), body, need))
                return false;
            const uint8_t* p = body;
            for (uint16_t i = 0; i < m_entry_count; i++) {
                memcpy(&m_entries[i], p, sizeof(RunEntry));
                p += sizeof(RunEntry);
                m_entry_slot[i] = *p++;
            }
        }
        /* Migrate to RRS2 on next persist. */
        return true;
    }

    return false;
}

bool RawRunStore::persist_index()
{
    if (!m_flash) return false;
    if (!m_flash->erase_block(m_index_addr)) {
        m_write_err++;
        return false;
    }

    IndexHeader ih;
    memset(&ih, 0, sizeof(ih));
    ih.magic = INDEX_MAGIC;
    ih.next_run_id = m_next_run_id;
    ih.entry_count = m_entry_count;
    ih.slot_used_mask = m_slot_used_mask;
    ih.max_slots = m_max_slots;

    /* Build payload: header + entries. Fits well under 4 KB for 32 slots. */
    uint8_t sector[FLASH_SECTOR_SIZE];
    memset(sector, 0xFF, sizeof(sector));
    memcpy(sector, &ih, sizeof(ih));
    size_t off = sizeof(ih);
    for (uint16_t i = 0; i < m_entry_count; i++) {
        if (off + sizeof(RunEntry) + 1 > sizeof(sector)) {
            m_write_err++;
            return false;
        }
        memcpy(sector + off, &m_entries[i], sizeof(RunEntry));
        off += sizeof(RunEntry);
        sector[off++] = m_entry_slot[i];
    }

    /* Program page-by-page (256 B). */
    size_t written = 0;
    size_t total = off;
    while (written < total) {
        size_t chunk = total - written;
        if (chunk > 256) chunk = 256;
        if (!m_flash->write_page(m_index_addr + (uint32_t)written,
                                 sector + written, chunk)) {
            m_write_err++;
            return false;
        }
        written += chunk;
    }
    return true;
}

bool RawRunStore::erase_range(uint32_t addr, uint32_t len)
{
    if (!m_flash || len == 0) return false;
    uint32_t end = addr + len;
    uint32_t a = addr & ~(RRS_SECTOR - 1u);
    for (; a < end; a += RRS_SECTOR) {
        if (!m_flash->erase_block(a)) {
            m_write_err++;
            return false;
        }
    }
    return true;
}

bool RawRunStore::ensure_erased(uint32_t need_end)
{
    if (need_end <= m_erased_end) return true;
    if (need_end > m_slot_end) need_end = m_slot_end;
    while (m_erased_end < need_end) {
        if (!m_flash->erase_block(m_erased_end)) {
            m_write_err++;
            return false;
        }
        m_erased_end += RRS_SECTOR;
    }
    return true;
}

bool RawRunStore::program_raw(uint32_t addr, const uint8_t* data, size_t len)
{
    if (!m_flash || !data || len == 0) return false;
    size_t off = 0;
    while (off < len) {
        uint32_t page_off = addr & 0xFFu;
        size_t space = 256u - (size_t)page_off;
        size_t chunk = len - off;
        if (chunk > space) chunk = space;
        if (!m_flash->write_page(addr, data + off, chunk)) {
            m_write_err++;
            return false;
        }
        addr += (uint32_t)chunk;
        off += chunk;
    }
    return true;
}

int RawRunStore::find_entry_idx(uint16_t run_id) const
{
    for (uint16_t i = 0; i < m_entry_count; i++)
        if (m_entries[i].run_id == run_id) return (int)i;
    return -1;
}

int RawRunStore::find_free_slot() const
{
    for (uint16_t s = 0; s < m_max_slots; s++)
        if ((m_slot_used_mask & (1u << s)) == 0) return (int)s;
    return -1;
}

int RawRunStore::find_oldest_entry_idx() const
{
    if (m_entry_count == 0) return -1;
    int best = 0;
    for (uint16_t i = 1; i < m_entry_count; i++) {
        if (m_entries[i].run_id < m_entries[best].run_id)
            best = (int)i;
    }
    return best;
}

void RawRunStore::remove_entry_at(int idx)
{
    if (idx < 0 || (uint16_t)idx >= m_entry_count) return;
    uint8_t slot = m_entry_slot[idx];
    m_slot_used_mask &= ~(1u << slot);
    for (uint16_t i = (uint16_t)idx; i + 1 < m_entry_count; i++) {
        m_entries[i] = m_entries[i + 1];
        m_entry_slot[i] = m_entry_slot[i + 1];
    }
    m_entry_count--;
}

void RawRunStore::delete_oldest_run()
{
    int i = find_oldest_entry_idx();
    if (i < 0) return;
    remove_entry_at(i);
    persist_index();
}

void RawRunStore::ensure_space_for_new_run()
{
    while (m_entry_count >= m_max_slots - 1 && m_entry_count > 0)
        delete_oldest_run();
}

bool RawRunStore::prepare_next_run()
{
    if (!m_ok || m_writing) return false;
    if (m_prepared) return true;

    ensure_space_for_new_run();
    int s = find_free_slot();
    if (s < 0) {
        delete_oldest_run();
        s = find_free_slot();
    }
    if (s < 0) return false;

    uint32_t base = slot_addr((uint8_t)s);
    if (!erase_range(base, m_slot_size)) return false;
    m_prep_slot = (uint8_t)s;
    m_prepared = true;
    Serial.print("{\"ev\":\"raw_prep\",\"slot\":");
    Serial.print((int)m_prep_slot);
    Serial.print(",\"prep_kb\":");
    Serial.print((long)(m_slot_size / 1024));
    Serial.print(",\"slot_kb\":");
    Serial.print((long)(m_slot_size / 1024));
    Serial.println("}");
    return true;
}

bool RawRunStore::create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy, uint32_t ts_utc)
{
    if (!m_ok || m_writing) return false;
    if (!m_prepared) {
        if (!prepare_next_run()) return false;
    }

    m_write_slot = m_prep_slot;
    m_prepared = false;
    m_slot_base = slot_addr(m_write_slot);
    m_slot_end = m_slot_base + m_slot_size;
    m_cursor = m_slot_base;
    m_erased_end = m_slot_end;
    m_payload_bytes = 0;
    m_run_crc = 0xFFFFFFFFu;
    m_write_run_id = m_next_run_id;
    m_pending_arm_side = arm_side;
    m_pending_baro_temp = baro_temp;
    m_pending_cal = cal_accuracy;
    m_pending_ts_utc = ts_utc;

    RunHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.format_ver = 2;
    hdr.arm_side = arm_side;
    hdr.ts_utc = ts_utc;
    hdr.baro_temp = baro_temp;
    hdr.data_size = 0;
    hdr.frame_count = 0;
    hdr.cal_accuracy = cal_accuracy;

    if (!program_raw(m_cursor, reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)))
        return false;
    m_cursor += sizeof(hdr);
    m_writing = true;
    return true;
}

bool RawRunStore::append_data(const uint8_t* data, size_t len)
{
    if (!m_writing || !data || len == 0) return false;
    uint32_t slot_end = m_slot_base + m_slot_size;
    if (m_cursor + len + CRC32_TRAILER_SIZE > slot_end) {
        m_write_err++;
        return false;
    }
    uint32_t want = m_cursor + (uint32_t)len + RRS_SECTOR;
    if (want > slot_end) want = slot_end;
    if (!ensure_erased(want)) return false;
    if (!program_raw(m_cursor, data, len)) return false;
    for (size_t i = 0; i < len; i++)
        m_run_crc = crc32_update(m_run_crc, data[i]);
    m_cursor += (uint32_t)len;
    m_payload_bytes += (uint32_t)len;
    return true;
}

uint16_t RawRunStore::close_run(uint32_t frame_count)
{
    if (!m_writing) return 0xFFFF;

    uint32_t final_crc = crc32_finalize(m_run_crc);
    uint8_t trail[6];
    trail[0] = CRC32_MAGIC_HI;
    trail[1] = CRC32_MAGIC_LO;
    trail[2] = (uint8_t)(final_crc);
    trail[3] = (uint8_t)(final_crc >> 8);
    trail[4] = (uint8_t)(final_crc >> 16);
    trail[5] = (uint8_t)(final_crc >> 24);

    if (!ensure_erased(m_cursor + 6)) {
        m_writing = false;
        return 0xFFFF;
    }
    bool ok = program_raw(m_cursor, trail, 6);
    m_cursor += 6;
    m_writing = false;

    if (!ok) return 0xFFFF;

    for (int i = (int)m_entry_count - 1; i >= 0; i--) {
        if (m_entry_slot[i] == m_write_slot)
            remove_entry_at(i);
    }

    if (m_entry_count >= m_max_slots)
        delete_oldest_run();

    RunEntry e;
    memset(&e, 0, sizeof(e));
    e.run_id = m_write_run_id;
    e.page_start = m_slot_base;
    e.page_end = m_cursor;
    e.timestamp = m_pending_ts_utc;
    e.arm_side = m_pending_arm_side;
    e.format_version = 2;
    e.compressed_size = m_payload_bytes;
    e.frame_count = frame_count;

    m_entries[m_entry_count] = e;
    m_entry_slot[m_entry_count] = m_write_slot;
    m_entry_count++;
    m_slot_used_mask |= (1u << m_write_slot);
    m_next_run_id++;

    if (!persist_index()) return 0xFFFF;
    return e.run_id;
}

uint8_t RawRunStore::flash_used_pct() const
{
    uint32_t used = (uint32_t)m_entry_count * m_slot_size;
    uint32_t total = (uint32_t)m_max_slots * m_slot_size;
    if (total == 0) return 0;
    uint32_t pct = (used * 100u) / total;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

uint32_t RawRunStore::oldest_run_age() const
{
    int i = find_oldest_entry_idx();
    if (i < 0) return 0;
    return m_entries[i].timestamp;
}

bool RawRunStore::read_run_header(uint16_t run_id, RunHeader& hdr) const
{
    const RunEntry* e = get_entry_by_id(run_id);
    if (!e || !m_flash) return false;
    if (!m_flash->read_data(e->page_start, reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)))
        return false;
    hdr.data_size = e->compressed_size;
    hdr.frame_count = (uint16_t)(e->frame_count > 65535 ? 65535 : e->frame_count);
    return true;
}

bool RawRunStore::read_run_data(uint16_t run_id, uint32_t offset, uint8_t* buf, size_t len) const
{
    const RunEntry* e = get_entry_by_id(run_id);
    if (!e || !m_flash || !buf) return false;
    uint32_t total = sizeof(RunHeader) + e->compressed_size + CRC32_TRAILER_SIZE;
    if (offset >= total) return false;
    size_t can = (size_t)(total - offset);
    if (len > can) len = can;
    return m_flash->read_data(e->page_start + offset, buf, len);
}

const RunEntry* RawRunStore::get_entry(uint16_t idx) const
{
    if (idx >= m_entry_count) return nullptr;
    return &m_entries[idx];
}

const RunEntry* RawRunStore::get_entry_by_id(uint16_t run_id) const
{
    int i = find_entry_idx(run_id);
    return (i < 0) ? nullptr : &m_entries[i];
}

const char* RawRunStore::build_run_list(char* buf, size_t buf_size) const
{
    if (!buf || buf_size < 3) return "[]";
    size_t n = 0;
    buf[n++] = '[';
    for (uint16_t i = 0; i < m_entry_count; i++) {
        char one[80];
        int w = snprintf(one, sizeof(one),
            "%s{\"id\":%u,\"ts\":%lu,\"size\":%lu,\"side\":\"%s\"}",
            (i ? "," : ""),
            (unsigned)m_entries[i].run_id,
            (unsigned long)m_entries[i].timestamp,
            (unsigned long)m_entries[i].compressed_size,
            m_entries[i].arm_side ? "right" : "left");
        if (w < 0 || n + (size_t)w + 2 >= buf_size) break;
        memcpy(buf + n, one, (size_t)w);
        n += (size_t)w;
    }
    buf[n++] = ']';
    buf[n] = '\0';
    return buf;
}

void RawRunStore::erase_all()
{
    m_entry_count = 0;
    m_slot_used_mask = 0;
    m_next_run_id = 0;
    m_writing = false;
    m_prepared = false;
    memset(m_entries, 0, sizeof(m_entries));
    persist_index();
}

void RawRunStore::list_files() const
{
    Serial.print("{\"ev\":\"raw_list\",\"n\":");
    Serial.print((int)m_entry_count);
    Serial.print(",\"mask\":");
    Serial.print((unsigned long)m_slot_used_mask);
    Serial.print(",\"slots\":");
    Serial.print((int)m_max_slots);
    Serial.println("}");
    for (uint16_t i = 0; i < m_entry_count; i++) {
        Serial.print("{\"ev\":\"raw_ent\",\"id\":");
        Serial.print((int)m_entries[i].run_id);
        Serial.print(",\"slot\":");
        Serial.print((int)m_entry_slot[i]);
        Serial.print(",\"sz\":");
        Serial.print((long)m_entries[i].compressed_size);
        Serial.print(",\"fr\":");
        Serial.print((long)m_entries[i].frame_count);
        Serial.println("}");
    }
}
