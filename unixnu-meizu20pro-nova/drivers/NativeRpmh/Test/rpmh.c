// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test: the RPMh command path against a command DB built here and a register file that sets the
// TCS completion bit from the delay callback, the way the RSC would between two polls.
#include "../Rpmh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) do { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); exit(1); } while (0)
#define CHECK(c) do { if (!(c)) FAIL("%s", #c); } while (0)

#define DB_CAP 4096
#define MEM_CAP 0x2000

static uint32_t mem_words[MEM_CAP / 4] __attribute__((aligned(8)));
static uint8_t db[DB_CAP] __attribute__((aligned(8)));

struct model {
    uint32_t irq_status;      // byte offset of IRQ_STATUS
    unsigned delays;
    int complete;             // set the completion bit on the first delay
    int armed;
};

static uint32_t *word(unsigned offset) { return (uint32_t *)((uint8_t *)mem_words + offset); }

static void model_delay(void *context, unsigned microseconds) {
    struct model *m = context;
    (void)microseconds;
    m->delays++;
    if (m->complete) *word(m->irq_status) |= 1u;
}

static void reset_mem(uint32_t version) {
    memset(mem_words, 0, sizeof mem_words);
    *word(0) = version;
}

// One VRM slave with two entries, laid out exactly as cmd-db.c parses it.
static size_t build_db(const char *const *names, const uint32_t *addresses, unsigned count) {
    const size_t header = 8 + 8 * 16 + 8;           // version, magic, 8 rsc_hdr, checksum, reserved
    const size_t entry = 24;                        // id, priority[2], addr, len, offset
    uint8_t *data = db + header;
    memset(db, 0, sizeof db);
    *(uint32_t *)db = 1;
    db[4] = 0xdb; db[5] = 0x30; db[6] = 0x03; db[7] = 0x0c;
    // slave[0] at offset 8, 16 bytes of __le16 fields.
    uint16_t *slave = (uint16_t *)(db + 8);
    slave[0] = 4;         // VRM
    slave[1] = 0;         // header_offset (relative to data)
    slave[2] = (uint16_t)(count * entry);
    slave[3] = (uint16_t)count;
    slave[4] = 0x0100;    // version 1.0
    for (unsigned i = 0; i < count; ++i) {
        uint8_t *e = data + i * entry;
        memcpy(e, names[i], strlen(names[i]));
        *(uint32_t *)(e + 16) = addresses[i];
        *(uint16_t *)(e + 20) = 4;
        *(uint16_t *)(e + 22) = 0;
    }
    return header + count * entry;
}

static void test_layout(void) {
    struct rpmh_layout l;
    CHECK(rpmh_layout_for(0x00020000, &l) == 0);
    CHECK(l.tcs_stride == 672 && l.cmd_stride == 20);
    CHECK(l.irq_enable == 0x00 && l.irq_status == 0x04 && l.irq_clear == 0x08);
    CHECK(l.wait_for_completion == 0x10 && l.control == 0x14 && l.status == 0x18 && l.cmd_enable == 0x1c);
    CHECK(l.cmd_msgid == 0x30 && l.cmd_addr == 0x34 && l.cmd_data == 0x38);
    CHECK(rpmh_layout_for(0x00030001, &l) == 0);
    CHECK(l.tcs_stride == 672 && l.cmd_stride == 24);
    CHECK(l.wait_for_completion == 0x20 && l.control == 0x24 && l.status == 0x28 && l.cmd_enable == 0x2c);
    CHECK(l.cmd_msgid == 0x34 && l.cmd_addr == 0x38 && l.cmd_data == 0x3c);
    CHECK(rpmh_layout_for(0x00010000, &l) == RPMH_EBADVERSION);
    CHECK(rpmh_layout_for(0x00040000, &l) == RPMH_EBADVERSION);
    CHECK(rpmh_layout_for(0, &l) == RPMH_EBADVERSION);
    CHECK(rpmh_layout_for(0, NULL) == RPMH_EINVAL);
    printf("rpmh layouts: ok\n");
}

static void test_cmd_db(void) {
    static const char *const names[2] = {"ldob9", "ldob12"};
    static const uint32_t addresses[2] = {0x41600, 0x40a00};
    const size_t size = build_db(names, addresses, 2);
    uint32_t address = 0;
    CHECK(rpmh_address(db, size, "ldob9", &address) == 0 && address == 0x41600);
    CHECK(rpmh_address(db, size, "ldob12", &address) == 0 && address == 0x40a00);
    CHECK(rpmh_address(db, size, "ldob8", &address) == RPMH_ENOENT);
    CHECK(rpmh_address(db, size, "ldob9x", &address) == RPMH_ENOENT);   // longer than a key
    CHECK(rpmh_address(db, size, "toolongkey", &address) == RPMH_EINVAL);
    CHECK(rpmh_address(db, 8 + 8 * 16 + 8 - 1, "ldob9", &address) == RPMH_ENODB);
    CHECK(rpmh_address(db, sizeof db, "ldob9", &address) == 0);
    db[4] ^= 0xff;
    CHECK(rpmh_address(db, size, "ldob9", &address) == RPMH_ENODB);
    db[4] ^= 0xff;
    // A count that runs past the buffer must not be walked.
    *(uint16_t *)(db + 8 + 6) = 0xffff;
    CHECK(rpmh_address(db, size, "ldob9", &address) == RPMH_ENOENT);
    *(uint16_t *)(db + 8 + 6) = 2;
    CHECK(rpmh_address(db, size, "ldob9", NULL) == RPMH_EINVAL);
    printf("rpmh command db: ok\n");
}

static void test_regulator(uint32_t version, unsigned cmd_stride, unsigned control, unsigned cmd_enable,
                           unsigned msgid, unsigned addr, unsigned data, unsigned wait) {
    static const char *const names[2] = {"ldob9", "ldob12"};
    static const uint32_t addresses[2] = {0x41600, 0x40a00};
    const size_t size = build_db(names, addresses, 2);
    struct model m = {.irq_status = RPMH_TCS_OFFSET + 0x04, .complete = 1};
    uint32_t found = 0, seen = 0;
    reset_mem(version);
    int rc = rpmh_regulator_on(mem_words, RPMH_TCS_OFFSET, db, size, "ldob9", 2960, model_delay, &m,
                               &found, &seen);
    CHECK(rc == 0);
    CHECK(found == 0x41600);
    CHECK(seen & 1u);
    CHECK(m.delays == 1);
    // Programmed registers, per command, before the reclaim cleared the enables.
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t base = RPMH_TCS_OFFSET + cmd_stride * i;
        CHECK(*word(base + msgid) == 0x10008);
        CHECK(*word(base + addr) == 0x41600 + 4 * i);
    }
    CHECK(*word(RPMH_TCS_OFFSET + data) == 2960);
    CHECK(*word(RPMH_TCS_OFFSET + cmd_stride + data) == 1);
    CHECK(*word(RPMH_TCS_OFFSET + 2 * cmd_stride + data) == 7);
    CHECK(*word(RPMH_TCS_OFFSET + wait) == 7);
    // Reclaimed: enables off, completion bit cleared, second TCS untouched.
    CHECK(*word(RPMH_TCS_OFFSET + control) == 0);
    CHECK(*word(RPMH_TCS_OFFSET + cmd_enable) == 0);
    CHECK(*word(RPMH_TCS_OFFSET + 0x08) == 1);      // the reclaim acknowledges the completion
    CHECK(*word(RPMH_TCS_OFFSET + 672 + control) == 0);
    // A version that does not decode must not touch the block.
    reset_mem(0x00010000);
    rc = rpmh_regulator_on(mem_words, RPMH_TCS_OFFSET, db, size, "ldob9", 2960, model_delay, &m, &found, &seen);
    CHECK(rc == RPMH_EBADVERSION);
    CHECK(*word(RPMH_TCS_OFFSET + cmd_enable) == 0);
    // A resource that is not a VRM must be refused before any write.
    static const char *const other[1] = {"cx.lvl"};
    static const uint32_t other_address[1] = {0x30000};
    reset_mem(version);
    CHECK(rpmh_regulator_on(mem_words, RPMH_TCS_OFFSET, db, size, "cx.lvl", 2960, model_delay, &m, &found, &seen) ==
          RPMH_ENOENT);
    const size_t other_size = build_db(other, other_address, 1);
    rc = rpmh_regulator_on(mem_words, RPMH_TCS_OFFSET, db, other_size, "cx.lvl", 2960, model_delay, &m, &found, &seen);
    CHECK(rc == RPMH_EBADADDR);
    CHECK(*word(RPMH_TCS_OFFSET + control) == 0);
    // No completion: the vote is still sent and the TCS is still reclaimed.
    const size_t size9 = build_db(names, addresses, 2);
    struct model silent = {.irq_status = RPMH_TCS_OFFSET + 0x04, .complete = 0};
    reset_mem(version);
    rc = rpmh_regulator_on(mem_words, RPMH_TCS_OFFSET, db, size9, "ldob12", 1800, model_delay, &silent, &found, &seen);
    CHECK(rc == RPMH_ETIMEOUT);
    CHECK(found == 0x40a00);
    CHECK(silent.delays == 200);
    CHECK(*word(RPMH_TCS_OFFSET + cmd_enable) == 0);
    CHECK(*word(RPMH_TCS_OFFSET + data) == 1800);
    printf("rpmh regulator v%u: ok\n", version >> 16);
}

int main(void) {
    test_layout();
    test_cmd_db();
    test_regulator(0x00020000, 20, 0x14, 0x1c, 0x30, 0x34, 0x38, 0x10);
    test_regulator(0x00030000, 24, 0x24, 0x2c, 0x34, 0x38, 0x3c, 0x20);
    printf("PASS: rpmh host model\n");
    return 0;
}
