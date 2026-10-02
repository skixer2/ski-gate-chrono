/**
 * @file    run_tests.cpp
 * @brief   BitPacker differential test: ArduinoFW C++ reference vs C port.
 *
 * Every sequence is fed to BOTH implementations; packed bytes, size and
 * type must be identical for every frame. Sequence 1 replays the B2a
 * golden vectors (real BHI260 frames captured 2026-10-02). Port outputs
 * are dumped to golden_packed.txt as durable packing vectors.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ref_bit_packer.h"
#include "bitpacker.h"

struct Case {
    std::string name;
    std::vector<RawFrame> frames;        /* input to both impls (same layout) */
    std::vector<uint32_t> ts;            /* ms timestamps */
};

static int failures = 0;

static void run_case(const Case &c, FILE *dump)
{
    BitPacker ref;
    struct sgc_bitpacker port;
    RawFrame rf;
    struct sgc_raw_frame pf;

    /* both start from the same reset state */
    ref.reset();
    sgc_bitpacker_reset(&port);

    for (size_t i = 0; i < c.frames.size(); i++) {
        rf = c.frames[i];
        pf.q_w = rf.q_w;  pf.q_x = rf.q_x;  pf.q_y = rf.q_y;  pf.q_z = rf.q_z;
        pf.la_x = rf.la_x; pf.la_y = rf.la_y; pf.la_z = rf.la_z;
        pf.baro_pa_div2 = rf.baro_pa_div2;

        uint8_t s_ref = ref.encode(rf, c.ts[i]);
        uint8_t s_port = sgc_bitpacker_encode(&port, &pf, c.ts[i]);
        const uint8_t *b_ref = ref.buffer();
        const uint8_t *b_port = port.buf;

        bool ok = (s_ref == s_port) && (ref.last_type() == port.last_type) &&
                  (memcmp(b_ref, b_port, 18) == 0);
        if (!ok) {
            failures++;
            printf("FAIL %s frame %zu: ref(sz=%u,t=%u) port(sz=%u,t=%u)\n  ref : ",
                   c.name.c_str(), i, s_ref, ref.last_type(), s_port, port.last_type);
            for (int k = 0; k < s_ref; k++) printf("%02x", b_ref[k]);
            printf("\n  port: ");
            for (int k = 0; k < s_port; k++) printf("%02x", b_port[k]);
            printf("\n");
        }
        if (dump) {
            fprintf(dump, "%s %zu t%u sz%u ", c.name.c_str(), i,
                    (unsigned)port.last_type, s_port);
            for (int k = 0; k < s_port; k++) fprintf(dump, "%02x", b_port[k]);
            fprintf(dump, "\n");
        }
    }
}

/* Parse golden vector jsonl (sid/hex/fw) into frame sequence.
 * RV event completes a frame: q from v[0..3] (x,y,z,w -> w,x,y,z),
 * la from latest LACC, baro from latest BARO (hold-last). */
static Case golden_case(const char *path)
{
    Case c;
    c.name = "GOLDEN";
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("CANNOT OPEN %s\n", path);
        exit(2);
    }
    RawFrame cur = {};
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int sid = 0;
        char hex[64] = "";
        int v[5] = {0};
        /* {"sid": 34, "hex": "...", "fw": [a, b, ...], ...} */
        if (sscanf(line, "{\"sid\": %d, \"hex\": \"%63[0-9a-f]\", \"fw\": [%d,%d,%d,%d,%d]",
                   &sid, hex, &v[0], &v[1], &v[2], &v[3], &v[4]) < 3) {
            continue;
        }
        if (sid == 34) {
            cur.q_w = (int16_t)v[3]; cur.q_x = (int16_t)v[0];
            cur.q_y = (int16_t)v[1]; cur.q_z = (int16_t)v[2];
            c.frames.push_back(cur);
            c.ts.push_back((uint32_t)(c.frames.size() * 10));
        } else if (sid == 31) {
            cur.la_x = (int16_t)v[0]; cur.la_y = (int16_t)v[1]; cur.la_z = (int16_t)v[2];
        } else if (sid == 129) {
            cur.baro_pa_div2 = (uint16_t)(v[0] / 2);
        }
        /* 128 (temp) not packed */
    }
    fclose(f);
    return c;
}

int main(int argc, char **argv)
{
    const char *golden_path = (argc > 1) ? argv[1] : "../golden/golden_vectors_20261002.jsonl";
    const char *dump_path = (argc > 2) ? argv[2] : "golden_packed.txt";
    FILE *dump = fopen(dump_path, "w");

    std::vector<Case> cases;

    /* 1: real captured frames */
    cases.push_back(golden_case(golden_path));

    /* 2: coasting — tiny deltas, forced anchor at 50 */
    {
        Case c; c.name = "COAST";
        RawFrame f = {15000, 100, -200, 30000, 5, -5, 7, 50000};
        for (int i = 0; i < 60; i++) {
            if (i) { f.q_w += (i & 1) ? 1 : -1; f.la_x += (i & 1) ? 1 : -1; }
            c.frames.push_back(f); c.ts.push_back((uint32_t)i * 10);
        }
        cases.push_back(c);
    }

    /* 3: classification boundaries */
    {
        Case c; c.name = "BOUND";
        RawFrame f0 = {1000, 0, 0, 0, 0, 0, 0, 40000};
        c.frames.push_back(f0); c.ts.push_back(0);            /* T3 first */
        RawFrame f = f0;
        f.q_w += 7;   c.frames.push_back(f); c.ts.push_back(10);   /* fits 4b */
        f.q_w += -15; c.frames.push_back(f); c.ts.push_back(20);   /* -8 fits 4b */
        f.q_x += 8;   c.frames.push_back(f); c.ts.push_back(30);   /* 8 -> 8b */
        f.q_y += -128; c.frames.push_back(f); c.ts.push_back(40);  /* 8b edge */
        f.q_z += 127; c.frames.push_back(f); c.ts.push_back(50);   /* anchor + 8b */
        f.la_y += 200; c.frames.push_back(f); c.ts.push_back(60);  /* -> T3 */
        cases.push_back(c);
    }

    /* 4: timestamp jump > 1023 ms -> T3 with header delta 0 */
    {
        Case c; c.name = "TJUMP";
        RawFrame f = {500, 500, 500, 500, 0, 0, 0, 30000};
        c.frames.push_back(f); c.ts.push_back(0);
        c.frames.push_back(f); c.ts.push_back(10);      /* coast T1 */
        c.frames.push_back(f); c.ts.push_back(5000);    /* jump -> T3, delta 0 */
        c.frames.push_back(f); c.ts.push_back(5010);    /* coast again */
        cases.push_back(c);
    }

    /* 5: baro delta boundaries */
    {
        Case c; c.name = "BARO";
        RawFrame f = {0, 0, 0, 16000, 0, 0, 0, 50000};
        c.frames.push_back(f); c.ts.push_back(0);
        f.baro_pa_div2 += 7;   c.frames.push_back(f); c.ts.push_back(10);
        f.baro_pa_div2 += 7;   c.frames.push_back(f); c.ts.push_back(20);   /* delta 7 fits */
        f.baro_pa_div2 += 8;   c.frames.push_back(f); c.ts.push_back(30);   /* -> 8b */
        f.baro_pa_div2 += 130; c.frames.push_back(f); c.ts.push_back(40);   /* -> T3 */
        cases.push_back(c);
    }

    for (auto &c : cases) {
        int before = failures;
        run_case(c, dump);
        printf("%-7s %3zu frames  %s\n", c.name.c_str(), c.frames.size(),
               failures == before ? "OK" : "MISMATCH");
    }
    if (dump) fclose(dump);
    printf("%s\n", failures == 0 ? "VERDICT: PASS - C port byte-identical to ArduinoFW"
                                 : "VERDICT: FAIL");
    return failures ? 1 : 0;
}
