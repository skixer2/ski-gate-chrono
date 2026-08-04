/**
 * @file    littlefs_storage.cpp
 * @brief   Run storage via mbed LittleFileSystem on MX25R1635F.
 *
 * Replaces FlashManager with wear-leveling filesystem.
 * LittleFS partitioned to sectors 4-511; flash ring uses sectors 0-3.
 *
 * V2.29: SPIFBlockDevice (explicit MX25R1635F on SPI0, CS_FLASH=p26).
 *        No diag prints — save heap for LittleFS caches.
 */

#include "littlefs_storage.h"
#include "spi_flash.h"
#include <Arduino.h>
#include <BlockDevice.h>
#include <SlicingBlockDevice.h>
#include <LittleFileSystem.h>
#include <File.h>
#include <Dir.h>
#include <cstring>
#include <cstdio>

using mbed::File;
using mbed::Dir;
using mbed::LittleFileSystem;
using mbed::SlicingBlockDevice;

/* POSIX flags (correct values, overriding any system defines) */
#ifdef O_RDONLY
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#endif
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#ifndef O_CREAT
#define O_CREAT   0x0100
#endif
#ifndef O_TRUNC
#define O_TRUNC   0x0200
#endif

/* ── Helpers ──────────────────────────────────────────────────── */
static void make_run_path(uint16_t id, char* buf, size_t sz) {
    snprintf(buf, sz, "run_%u.dat", (unsigned)id);
}

LittleFSStorage::LittleFSStorage()
    : m_fs(nullptr), m_bd(nullptr), m_file(nullptr), m_file_open(false)
    , m_run_count(0), m_next_run_id(0), m_run_bytes(0), m_run_crc(0xFFFFFFFF)
    , m_write_buf_pos(0)
    , m_entry_count(0)
{
    memset(m_write_buf, 0, sizeof(m_write_buf));
    memset(m_entries, 0, sizeof(m_entries));
}

LittleFSStorage::~LittleFSStorage() {
    if (m_file_open && m_file) {
        static_cast<File*>(m_file)->close();
        delete static_cast<File*>(m_file);
    }
    if (m_fs) {
        auto* fs = static_cast<LittleFileSystem*>(m_fs);
        fs->unmount();
        delete fs;
    }
    // m_bd is owned by SPIFlash — do NOT delete
}

/* ── begin() ──────────────────────────────────────────────────── */
bool LittleFSStorage::begin() {
    extern SPIFlash g_flash;
    m_bd = g_flash.get_bd();
    if (!m_bd) return false;
    auto* raw = static_cast<mbed::BlockDevice*>(m_bd);

    /* BD is already init'd by SPIFlash::begin().  mbed SPIFBlockDevice
       init/deinit are ref-counted — a second init() from mount() just
       bumps the count.  No deinit needed. */

    /* Slice sectors 6–507 (reserve 508–511 for config).
       Flash ring uses sectors 0-5 (v4.07: 20-byte RingEntry).
       For MX25R1635F (2MB): 502 sectors × 4096 bytes = 0x1F6000. */
    uint32_t stop = 0x1FC000;
    auto* sliced = new SlicingBlockDevice(raw, 0x6000, stop);

    /* LittleFS v1 — Arduino example baseline, no v2 metadata-pair bug. */
    auto* fs = new LittleFileSystem("littlefs");
    if (!fs) { delete sliced; return false; }

    int err = fs->mount(sliced);
    if (err != 0) {
        Serial.print("{\"ev\":\"fs_mount_fail\",\"err\":");
        Serial.print(err);
        Serial.print(",\"blk\":"); Serial.print(sliced->size() / 4096);
        Serial.print(",\"sz\":"); Serial.print(sliced->size());
        Serial.print(",\"es\":"); Serial.print(sliced->get_erase_size());
        Serial.println("}");
        /* V2.91: Auto-reformat — simplest path. DP should prevent
           corruption across reset. If mount still fails, reformat. */
        Serial.print("{\"ev\":\"fs_reformat\",\"reason\":\"mount_fail_");
        Serial.print(err); Serial.println("\"}");
        int fmt_err = fs->reformat(sliced);
        if (fmt_err != 0) {
            Serial.print("{\"ev\":\"fs_reformat_fail\",\"err\":");
            Serial.print(fmt_err); Serial.println("}");
            delete fs; delete sliced;
            return false;
        }
        err = fs->mount(sliced);
        if (err != 0) {
            Serial.print("{\"ev\":\"fs_remount_fail\",\"err\":");
            Serial.print(err); Serial.println("}");
            delete fs; delete sliced;
            return false;
        }
    }
    m_fs = fs;
    scan_runs();
    return true;
}

/* ── scan_runs() ─────────────────────────────────────────────── */
void LittleFSStorage::scan_runs() {
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    m_entry_count = 0; m_next_run_id = 0;
    memset(m_entries, 0, sizeof(m_entries));
    Dir dir;
    int dir_err = dir.open(fs, "/");
    if (dir_err != 0) {
        Serial.print("{\"ev\":\"scan_err\",\"err\":");
        Serial.print(dir_err);
        Serial.println("}");
        return;
    }
    struct dirent ent;
    while (dir.read(&ent) > 0) {
        unsigned id = 0;
        if (sscanf(ent.d_name, "run_%5u.dat", &id) != 1 || id > 65535) continue;
        char path[32]; make_run_path((uint16_t)id, path, sizeof(path));
        File file;
        if (file.open(fs, path, O_RDONLY) != 0) continue;
        RunHeader hdr;
        if (file.read(&hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) { file.close(); continue; }
        ssize_t file_sz = file.size();
        file.close();
        if (hdr.format_ver < 1 || hdr.format_ver > 3) continue;
        if (hdr.arm_side > 1) continue;
        uint32_t real_data_size = hdr.data_size;
        uint16_t real_frame_count = hdr.frame_count;
        if (hdr.format_ver >= 2) {
            if (file_sz < 0 || (size_t)file_sz < sizeof(RunHeader) + CRC32_TRAILER_SIZE) continue;
            real_data_size = (uint32_t)file_sz - sizeof(RunHeader) - CRC32_TRAILER_SIZE;
            real_frame_count = 0;
        } else if (hdr.data_size == 0) continue;
        if ((uint16_t)id >= m_next_run_id) m_next_run_id = (uint16_t)id + 1;
        if (m_entry_count < MAX_ENTRIES) {
            RunEntry& e = m_entries[m_entry_count++];
            e.run_id = (uint16_t)id; e.timestamp = hdr.ts_utc;
            e.arm_side = hdr.arm_side; e.format_version = hdr.format_ver;
            e.compressed_size = real_data_size; e.frame_count = real_frame_count;
        }
    }
    dir.close();

    Serial.print("{\"ev\":\"scan_summary\",\"entries\":");
    Serial.print(m_entry_count);
    Serial.println("}");
    for (uint16_t i = 1; i < m_entry_count; i++) {
        RunEntry key = m_entries[i]; int16_t j = (int16_t)i - 1;
        while (j >= 0 && m_entries[j].run_id > key.run_id)
            { m_entries[j+1] = m_entries[j]; j--; }
        m_entries[j+1] = key;
    }
}

/* ── create_run() ────────────────────────────────────────────── */
bool LittleFSStorage::create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy) {
    static_assert(sizeof(RunHeader) == 16, "RunHeader must be 16 bytes packed");
    static_assert(WRITE_BUF_SIZE >= 16, "write buf must hold RunHeader");
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) {
        Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_NO_FS\"}");
        return false;
    }
    if (m_file_open) {
        Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_ALREADY_OPEN\"}");
        return false;
    }

    auto* f = new File();
    char path[32]; make_run_path(m_next_run_id, path, sizeof(path));
    /* V4.50: pure append handle. NEVER seek/read the write fd.
       v4.49 verified header in-handle then still dumped ver=0 — LittleFS v1
       + seek/read on the writer corrupts or confuses the first extent. */
    int err = f->open(fs, path, O_WRONLY | O_CREAT | O_TRUNC);
    if (err != 0) {
        Serial.print("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_OPEN_ERR_");
        Serial.print(err); Serial.println("\"}");
        delete f; return false;
    }

    /* Build wire header into the normal write buffer and flush once.
       Single write path for header + payload (no separate f->write). */
    memset(m_write_buf, 0, WRITE_BUF_SIZE);
    m_write_buf[0] = 2;                      /* format_ver */
    m_write_buf[1] = arm_side;
    m_write_buf[6] = (uint8_t)(baro_temp & 0xFF);
    m_write_buf[7] = (uint8_t)((baro_temp >> 8) & 0xFF);
    m_write_buf[14] = cal_accuracy;
    m_write_buf_pos = 16;

    m_file = f;
    m_file_open = true;
    m_run_bytes = 0;          /* header not counted in compressed size */
    m_run_crc = 0xFFFFFFFF;   /* CRC covers compressed frames only */
    m_last_sync_bytes = 0;
    m_pending_arm_side = arm_side;
    m_pending_baro_temp = baro_temp;
    m_pending_cal_accuracy = cal_accuracy;

    /* Force header to media before any frame data. */
    flush_write_buf();
    int sc = f->sync();
    if (sc != 0) {
        Serial.print("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_SYNC_ERR_");
        Serial.print(sc); Serial.println("\"}");
        f->close(); delete f;
        m_file = nullptr; m_file_open = false;
        fs->remove(path);
        return false;
    }

    /* Verify on a SEPARATE read-only handle — never touch writer position. */
    uint8_t chk[16];
    memset(chk, 0xFF, sizeof(chk));
    ssize_t nr = -1;
    {
        File rf;
        if (rf.open(fs, path, O_RDONLY) == 0) {
            nr = rf.read(chk, sizeof(chk));
            rf.close();
        }
    }

    Serial.print("{\"ev\":\"close_trace\",\"step\":\"hdr_write\",\"ok\":");
    bool ok = (nr == 16 && chk[0] == 2 && chk[1] == arm_side);
    Serial.print(ok ? "true" : "false");
    Serial.print(",\"nr\":"); Serial.print((long)nr);
    Serial.print(",\"ver\":"); Serial.print((long)chk[0]);
    Serial.print(",\"side\":"); Serial.print((long)chk[1]);
    Serial.print(",\"bt\":"); Serial.print((long)baro_temp);
    Serial.print(",\"hex\":\"");
    for (int i = 0; i < 16; i++) {
        static const char* H = "0123456789abcdef";
        Serial.print(H[(chk[i] >> 4) & 0xF]);
        Serial.print(H[chk[i] & 0xF]);
    }
    Serial.println("\"}");

    if (!ok) {
        /* Last-resort: close writer, rewrite file head via fresh RDWR, reopen append. */
        f->close();
        File wf;
        bool repaired = false;
        if (wf.open(fs, path, O_RDWR) == 0) {
            uint8_t wire[16];
            memset(wire, 0, sizeof(wire));
            wire[0] = 2; wire[1] = arm_side;
            wire[6] = (uint8_t)(baro_temp & 0xFF);
            wire[7] = (uint8_t)((baro_temp >> 8) & 0xFF);
            wire[14] = cal_accuracy;
            if (wf.seek(0, SEEK_SET) == 0 &&
                wf.write(wire, 16) == 16 &&
                wf.sync() == 0) {
                memset(chk, 0xFF, sizeof(chk));
                if (wf.seek(0, SEEK_SET) == 0)
                    nr = wf.read(chk, 16);
                if (nr == 16 && chk[0] == 2) repaired = true;
            }
            wf.close();
        }
        if (!repaired) {
            Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_HDR_BAD\"}");
            fs->remove(path);
            delete f;
            m_file = nullptr; m_file_open = false;
            return false;
        }
        Serial.println("{\"ev\":\"close_trace\",\"step\":\"hdr_repair\",\"ok\":true}");
        /* Reopen append writer at EOF */
        if (f->open(fs, path, O_WRONLY) != 0) {
            Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_REOPEN_W_ERR\"}");
            fs->remove(path);
            delete f;
            m_file = nullptr; m_file_open = false;
            return false;
        }
        off_t end = f->seek(0, SEEK_END);
        if (end < 16) {
            Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"CR_SEEK_END_ERR\"}");
            f->close(); fs->remove(path); delete f;
            m_file = nullptr; m_file_open = false;
            return false;
        }
        m_file = f; m_file_open = true;
    }

    return true;
}

/* ── append_data() ───────────────────────────────────────────── */
bool LittleFSStorage::append_data(const uint8_t* data, size_t len) {
    if (!m_file_open) return false;
    for (size_t i = 0; i < len; i++) {
        if (m_write_buf_pos >= WRITE_BUF_SIZE) flush_write_buf();
        m_write_buf[m_write_buf_pos++] = data[i];
    }
    m_run_bytes += len;
    for (size_t i = 0; i < len; i++) m_run_crc = crc32_update(m_run_crc, data[i]);
    return true;
}

void LittleFSStorage::flush_write_buf() {
    if (m_write_buf_pos == 0) return;
    auto* f = static_cast<File*>(m_file);
    ssize_t written = f->write(m_write_buf, m_write_buf_pos);
    if (written != (ssize_t)m_write_buf_pos) {
        Serial.print("{\"ev\":\"write_err\",\"expected\":");
        Serial.print((long)m_write_buf_pos);
        Serial.print(",\"got\":");
        Serial.print((long)written);
        Serial.println("}");
    }
    /* V4.42: Periodic sync every 4 KB.
       Without this, close_run() must sync 20-40 KB in one shot which
       can overwhelm LittleFS on nRF52 (file close returns error). */
    if (m_run_bytes - m_last_sync_bytes >= 4096) {
        f->sync();
        m_last_sync_bytes = m_run_bytes;
    }
    m_write_buf_pos = 0;
}

/* ── close_run() ─────────────────────────────────────────────── */
uint16_t LittleFSStorage::close_run(uint32_t frame_count) {
    char buf[100];
    snprintf(buf, sizeof(buf),
        "{\"ev\":\"close_trace\",\"step\":\"start\",\"sz\":%lu,\"fr\":%lu,\"wh_before\":%d}",
        (unsigned long)m_run_bytes, (unsigned long)frame_count, m_file_open ? 1 : 0);
    Serial.println(buf); Serial.flush(); delay(50);
    if (!m_file_open) {
        Serial.println("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":-1,\"run_count\":-1,\"final_wh\":\"NOT_OPEN\"}");
        Serial.flush();
        return 0xFFFF;
    }
    auto* f  = static_cast<File*>(m_file);
    uint16_t run_id = m_next_run_id;
    uint32_t run_sz = m_run_bytes;   /* save before zeroing */
    uint32_t crc = m_run_crc ^ 0xFFFFFFFF;
    uint8_t trailer[6] = {
        CRC32_MAGIC_HI, CRC32_MAGIC_LO,
        (uint8_t)(crc & 0xFF), (uint8_t)((crc>>8) & 0xFF),
        (uint8_t)((crc>>16) & 0xFF), (uint8_t)((crc>>24) & 0xFF)
    };
    /* ── V2.80 (H2 fix): APPEND-ONLY close ──
       NO seek(0)+write(header). The header was written at create_run()
       with data_size=0,frame_count=0 and stays that way permanently.
       Eliminates the LittleFS COW cascade:
         - Old: flush→write_CRC→sync→seek(0)→rewrite_16B_header→
                COW_copy_entire_file→sync→close
         - New: flush→write_CRC→sync→close  (no COW, no double sync)
       If power is lost during close, the file is truncated at the
       last-synced point. The CRC trailer may or may not be present,
       but the header is never touched → no data_size=0 corruption. */
    Serial.println("{\"ev\":\"cbc\",\"at\":\"pre_flush\"}"); Serial.flush();
    flush_write_buf();
    Serial.println("{\"ev\":\"cbc\",\"at\":\"post_flush\"}"); Serial.flush();
    Serial.println("{\"ev\":\"cbc\",\"at\":\"trailer_write\"}"); Serial.flush();
    ssize_t tw = f->write(trailer, 6);
    Serial.println("{\"ev\":\"cbc\",\"at\":\"trailer_sync\"}"); Serial.flush();
    int sc = f->sync();
    Serial.println("{\"ev\":\"cbc\",\"at\":\"close\"}"); Serial.flush();
    int cc = f->close();
    delete f;
    m_file = nullptr; m_file_open = false;

    /* V4.44: Single stat after close. Accept the run if the file exists
       with plausible size even when write/sync/close returned an error
       code — LittleFS on nRF52 can return non-zero after a good write. */
    Serial.println("{\"ev\":\"cbc\",\"at\":\"stat_verify\"}"); Serial.flush();
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    char rpath[32]; make_run_path(run_id, rpath, sizeof(rpath));
    struct stat st;
    int stat_err = fs->stat(rpath, &st);
    Serial.print("{\"ev\":\"cbc\",\"at\":\"probe\",\"err\":");
    Serial.print(stat_err);
    if (stat_err == 0) {
        Serial.print(",\"sz\":"); Serial.print((long)st.st_size);
    }
    Serial.println("}");

    /* V4.50: after close, read header on a fresh fd. If format_ver!=2,
       rewrite the 16-byte head with the authoritative RAM metadata.
       LittleFS v1 tolerates this short in-place head rewrite; v2 COW
       was the old danger, and we are on v1. */
    if (stat_err == 0) {
        uint8_t hb[16];
        memset(hb, 0xFF, sizeof(hb));
        ssize_t hr = -1;
        {
            File rf;
            if (rf.open(fs, rpath, O_RDONLY) == 0) {
                hr = rf.read(hb, sizeof(hb));
                rf.close();
            }
        }
        Serial.print("{\"ev\":\"close_trace\",\"step\":\"hdr_after_close\",\"nr\":");
        Serial.print((long)hr);
        Serial.print(",\"ver\":"); Serial.print((long)(hr > 0 ? hb[0] : -1));
        Serial.print(",\"side\":"); Serial.print((long)(hr > 1 ? hb[1] : -1));
        Serial.print(",\"hex\":\"");
        for (int i = 0; i < 16; i++) {
            static const char* H = "0123456789abcdef";
            Serial.print(H[(hb[i] >> 4) & 0xF]);
            Serial.print(H[hb[i] & 0xF]);
        }
        Serial.println("\"}");

        /* Always rewrite the 16-byte head on close (LittleFS v1).
           Keeps data_size/frame_count = 0 (append-only size via file size),
           but forces format_ver=2 even if create-time head was lost. */
        {
            long pre_ver = (hr > 0) ? (long)hb[0] : -1L;
            uint8_t wire[16];
            memset(wire, 0, sizeof(wire));
            wire[0] = 2;
            wire[1] = m_pending_arm_side;
            wire[6] = (uint8_t)(m_pending_baro_temp & 0xFF);
            wire[7] = (uint8_t)((m_pending_baro_temp >> 8) & 0xFF);
            wire[14] = m_pending_cal_accuracy;

            File wf;
            bool fixed = false;
            if (wf.open(fs, rpath, O_RDWR) == 0) {
                if (wf.seek(0, SEEK_SET) == 0 &&
                    wf.write(wire, 16) == 16 &&
                    wf.sync() == 0) {
                    memset(hb, 0xFF, sizeof(hb));
                    if (wf.seek(0, SEEK_SET) == 0)
                        hr = wf.read(hb, 16);
                    fixed = (hr == 16 && hb[0] == 2);
                }
                wf.close();
            }
            Serial.print("{\"ev\":\"close_trace\",\"step\":\"hdr_fix\",\"ok\":");
            Serial.print(fixed ? "true" : "false");
            Serial.print(",\"pre_ver\":"); Serial.print(pre_ver);
            Serial.print(",\"ver\":"); Serial.print((long)hb[0]);
            Serial.print(",\"hex\":\"");
            for (int i = 0; i < 16; i++) {
                static const char* H = "0123456789abcdef";
                Serial.print(H[(hb[i] >> 4) & 0xF]);
                Serial.print(H[hb[i] & 0xF]);
            }
            Serial.println("\"}");
        }
    }

    bool file_ok = (stat_err == 0 && st.st_size > (off_t)(sizeof(RunHeader) + 6));
    if (tw != 6 || sc != 0 || cc != 0) {
        Serial.print("{\"ev\":\"close_trace\",\"step\":\"done\",");
        Serial.print("\"final_wh\":\"");
        Serial.print(file_ok ? "CLOSE_WARN" : "CLOSE_ERR");
        Serial.print("\",\"tw\":");
        Serial.print((long)tw); Serial.print(",\"sc\":");
        Serial.print(sc); Serial.print(",\"cc\":");
        Serial.print(cc); Serial.print(",\"stat\":");
        Serial.print(stat_err);
        if (stat_err == 0) { Serial.print(",\"fsz\":"); Serial.print((long)st.st_size); }
        Serial.println("}");
        Serial.flush();
        if (!file_ok) return 0xFFFF;
        /* file exists with data — continue and index it */
    }

    int idx = find_entry_idx(run_id);
    if (idx < 0 && m_entry_count < MAX_ENTRIES) {
        idx = m_entry_count; m_entries[idx].run_id = run_id; m_entry_count++;
    }
    if (idx >= 0) {
        m_entries[idx].timestamp = 0;
        m_entries[idx].arm_side = m_pending_arm_side;
        m_entries[idx].format_version = 2;
        m_entries[idx].compressed_size = run_sz;
        m_entries[idx].frame_count = frame_count;
    }
    m_next_run_id++; m_run_bytes = 0; m_run_crc = 0xFFFFFFFF;
    Serial.print("{\"ev\":\"close_trace\",\"step\":\"done\",\"entry_count\":");
    Serial.print(m_entry_count); Serial.print(",\"run_count\":");
    Serial.print(m_next_run_id); Serial.println("}");
    /* V4.03: run_saved is emitted by main.cpp (single source of truth).
       The duplicate run_saved here was stale (wh:0) and was picked up
       first by the test harness, masking the real values. */
    return run_id;
}

/* ── Queries ────────────────────────────────────────────────── */
uint8_t LittleFSStorage::flash_used_pct() const {
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) return 0;
    struct statvfs st; memset(&st, 0, sizeof(st));
    if (fs->statvfs("/", &st) != 0 || st.f_blocks == 0) return 0;
    uint64_t pct = ((st.f_blocks - st.f_bfree) * 100) / st.f_blocks;
    return (uint8_t)(pct > 100 ? 100 : pct);
}
uint32_t LittleFSStorage::oldest_run_age() const {
    return m_entry_count > 0 ? m_entries[0].timestamp : 0;
}

/* ── File access ────────────────────────────────────────────── */
bool LittleFSStorage::read_run_header(uint16_t run_id, RunHeader& hdr) const {
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) return false;
    char path[32]; make_run_path(run_id, path, sizeof(path));
    File file;
    if (file.open(fs, path, O_RDONLY) != 0) return false;
    bool ok = (file.read(&hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));
    /* V2.80/V4.46: format_ver>=2 is append-only — on-disk data_size=0.
       Prefer RAM index (set at close_run from m_run_bytes). Fall back to
       file.size() / seek END, because mbed File::size() can return 0. */
    if (ok && hdr.format_ver >= 2 && hdr.data_size == 0) {
        const RunEntry* e = get_entry_by_id(run_id);
        if (e && e->compressed_size > 0) {
            hdr.data_size = e->compressed_size;
        } else {
            ssize_t file_sz = file.size();
            if (file_sz <= 0) {
                off_t end = file.seek(0, SEEK_END);
                if (end > 0) file_sz = (ssize_t)end;
                file.seek((off_t)sizeof(RunHeader), SEEK_SET); /* restore */
            }
            if (file_sz > 0 && (size_t)file_sz > sizeof(RunHeader) + CRC32_TRAILER_SIZE) {
                hdr.data_size = (uint32_t)file_sz - sizeof(RunHeader) - CRC32_TRAILER_SIZE;
            }
        }
    }
    file.close(); return ok;
}
bool LittleFSStorage::read_run_data(uint16_t run_id, uint32_t offset, uint8_t* buf, size_t len) const {
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) return false;
    char path[32]; make_run_path(run_id, path, sizeof(path));
    File file;
    if (file.open(fs, path, O_RDONLY) != 0) return false;
    if (file.seek((off_t)offset, SEEK_SET) != (off_t)offset) { file.close(); return false; }
    bool ok = (file.read(buf, len) == (ssize_t)len);
    file.close(); return ok;
}

/* ── Run list ───────────────────────────────────────────────── */
const RunEntry* LittleFSStorage::get_entry(uint16_t idx) const {
    return (idx < m_entry_count) ? &m_entries[idx] : nullptr;
}
const RunEntry* LittleFSStorage::get_entry_by_id(uint16_t run_id) const {
    for (uint16_t i = 0; i < m_entry_count; i++)
        if (m_entries[i].run_id == run_id) return &m_entries[i];
    return nullptr;
}
const char* LittleFSStorage::build_run_list(char* buf, size_t sz) const {
    int p = snprintf(buf, sz, "[");
    for (uint16_t i = 0; i < m_entry_count; i++) {
        const RunEntry& e = m_entries[i];
        p += snprintf(buf+p, sz-p, "%s{\"id\":%u,\"ts\":%lu,\"size\":%lu,\"side\":\"%s\"}",
            i ? "," : "", e.run_id, (unsigned long)e.timestamp,
            (unsigned long)e.compressed_size, e.arm_side ? "right" : "left");
        if ((size_t)p >= sz - 2) break;
    }
    snprintf(buf+p, sz-p, "]");
    return buf;
}

/* ── Entry management ───────────────────────────────────────── */
int LittleFSStorage::find_entry_idx(uint16_t run_id) const {
    for (uint16_t i = 0; i < m_entry_count; i++)
        if (m_entries[i].run_id == run_id) return (int)i;
    return -1;
}
void LittleFSStorage::remove_entry_at(int idx) {
    if (idx < 0 || idx >= (int)m_entry_count) return;
    for (uint16_t i = (uint16_t)idx; i < m_entry_count - 1; i++)
        m_entries[i] = m_entries[i+1];
    m_entry_count--;
    memset(&m_entries[m_entry_count], 0, sizeof(RunEntry));
}
void LittleFSStorage::delete_oldest_run() {
    if (m_entry_count == 0) return;
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) return;
    uint16_t oldest = 0;
    for (uint16_t i = 1; i < m_entry_count; i++)
        if (m_entries[i].timestamp < m_entries[oldest].timestamp) oldest = i;
    char path[32]; make_run_path(m_entries[oldest].run_id, path, sizeof(path));
    fs->remove(path);
    remove_entry_at((int)oldest);
}

/* ── Factory reset ──────────────────────────────────────────── */
void LittleFSStorage::unmount() {
    if (m_file_open && m_file) {
        static_cast<File*>(m_file)->close();
        delete static_cast<File*>(m_file);
        m_file = nullptr; m_file_open = false;
    }
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (fs) {
        fs->unmount();
    }
}

void LittleFSStorage::metadata_sync() {
    /* Opening root dir for reading triggers a metadata commit
       in littlefs, flushing pending superblock/directory changes.
       lfs_unmount() alone does NOT commit metadata. */
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (fs) {
        Dir dir;
        dir.open(fs, "/");
        dir.close();
    }
}

void LittleFSStorage::erase_all() {
    /* Close any open file (C++ cleanup only, safe). */
    if (m_file_open && m_file) {
        static_cast<File*>(m_file)->close();
        delete static_cast<File*>(m_file);
        m_file = nullptr; m_file_open = false;
    }
    /* Erase raw sectors directly — NO LittleFS operations.
       fs->unmount() / fs->reformat() can HardFault on corrupt metadata.
       Boot init handles fresh format+mount. */
    if (m_bd) {
        auto* raw = static_cast<mbed::BlockDevice*>(m_bd);
        for (uint32_t addr = 0x6000; addr < 0x14000; addr += 4096) {
            raw->erase(addr, 4096);
        }
    }
    memset(m_entries, 0, sizeof(m_entries));
    m_entry_count = 0; m_next_run_id = 0; m_run_count = 0;
    m_run_bytes = 0; m_run_crc = 0xFFFFFFFF;
}

void LittleFSStorage::list_files() const {
    auto* fs = static_cast<LittleFileSystem*>(m_fs);
    if (!fs) { Serial.println("{\"ev\":\"ls\",\"err\":\"fs_null\"}"); return; }
    Dir dir;
    if (dir.open(fs, "/") != 0) { Serial.println("{\"ev\":\"ls\",\"err\":\"dir_open_failed\"}"); return; }
    Serial.print("{\"ev\":\"ls\",\"files\":[");
    bool first = true;
    struct dirent ent;
    while (dir.read(&ent) > 0) {
        if (!first) Serial.print(",");
        Serial.print("\""); Serial.print(ent.d_name); Serial.print("\"");
        first = false;
    }
    dir.close();
    Serial.println("]}");
}

/* ── CRC32 ──────────────────────────────────────────────────── */
uint32_t LittleFSStorage::crc32_update(uint32_t crc, uint8_t byte) {
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
uint32_t LittleFSStorage::crc32_buffer(const uint8_t* data, size_t len) {
    uint32_t crc = crc32_initial();
    for (size_t i = 0; i < len; i++) crc = crc32_update(crc, data[i]);
    return crc32_finalize(crc);
}
