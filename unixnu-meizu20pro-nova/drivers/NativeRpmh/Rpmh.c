// SPDX-License-Identifier: BSD-2-Clause-Patent
// RPMh command path of the application-processor RSC. Register offsets and the message encoding come
// from drivers/soc/qcom/rpmh-rsc.c (v2.7 and v3.0 tables); the command DB layout from cmd-db.c.
#include "Rpmh.h"

#include <string.h>

#define RPMH_REGULATOR_ON 1u
#define RPMH_REGULATOR_MODE_LDO_HPM 7u
#define RPMH_TCS_STRIDE 672u
#define RPMH_POLL_MS 200u
#define RPMH_MAX_ENTRIES 512u

struct rpmh_cmd_db_slave {
    uint16_t id, header_offset, data_offset, count, version, reserved[3];
} __attribute__((packed));

struct rpmh_cmd_db_header {
    uint32_t version;
    uint8_t magic[4];
    struct rpmh_cmd_db_slave slave[RPMH_MAX_SLAVES];
    uint32_t checksum, reserved;
} __attribute__((packed));

struct rpmh_cmd_db_entry {
    char id[8];
    uint32_t priority[2];
    uint32_t address;
    uint16_t length, offset;
} __attribute__((packed));

static inline void barrier(void) {
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static uint32_t rd32(const volatile uint32_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)((const volatile char *)base + offset);
}

static void wr32(volatile uint32_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)((volatile char *)base + offset) = value;
    barrier();
}

int rpmh_layout_for(uint32_t version, struct rpmh_layout *layout) {
    unsigned major = (version >> 16) & 0xffu;
    if (!layout) return RPMH_EINVAL;
    if (major == 2) {
        *layout = (struct rpmh_layout){
            .tcs_stride = RPMH_TCS_STRIDE, .cmd_stride = 20,
            .irq_enable = 0x00, .irq_status = 0x04, .irq_clear = 0x08,
            .wait_for_completion = 0x10, .control = 0x14, .status = 0x18, .cmd_enable = 0x1c,
            .cmd_msgid = 0x30, .cmd_addr = 0x34, .cmd_data = 0x38,
        };
        return 0;
    }
    if (major == 3) {
        *layout = (struct rpmh_layout){
            .tcs_stride = RPMH_TCS_STRIDE, .cmd_stride = 24,
            .irq_enable = 0x00, .irq_status = 0x04, .irq_clear = 0x08,
            .wait_for_completion = 0x20, .control = 0x24, .status = 0x28, .cmd_enable = 0x2c,
            .cmd_msgid = 0x34, .cmd_addr = 0x38, .cmd_data = 0x3c,
        };
        return 0;
    }
    return RPMH_EBADVERSION;
}

int rpmh_address(const void *cmd_db, size_t size, const char *name, uint32_t *address) {
    static const uint8_t magic[4] = {0xdb, 0x30, 0x03, 0x0c};
    char query[8] = {0};
    const struct rpmh_cmd_db_header *header;
    const uint8_t *data;
    size_t data_size, length;

    if (!cmd_db || !name || !address) return RPMH_EINVAL;
    for (length = 0; name[length]; ++length) {
        if (length == sizeof query) return RPMH_EINVAL;
    }
    if (size < sizeof *header) return RPMH_ENODB;
    header = cmd_db;
    if (memcmp(header->magic, magic, sizeof magic)) return RPMH_ENODB;
    memcpy(query, name, length);
    data = (const uint8_t *)cmd_db + sizeof *header;
    data_size = size - sizeof *header;
    for (unsigned s = 0; s < RPMH_MAX_SLAVES; ++s) {
        const struct rpmh_cmd_db_slave *slave = &header->slave[s];
        const struct rpmh_cmd_db_entry *entries;
        size_t table = slave->header_offset, count = slave->count;
        if (!slave->id) break;
        if (!count || count > RPMH_MAX_ENTRIES || table >= data_size) continue;
        if (count > (data_size - table) / sizeof *entries) continue;
        entries = (const struct rpmh_cmd_db_entry *)(data + table);
        for (unsigned i = 0; i < count; ++i) {
            if (!memcmp(entries[i].id, query, sizeof query)) {
                *address = entries[i].address;
                return 0;
            }
        }
    }
    return RPMH_ENOENT;
}

// Programs one TCS and triggers it. The caller has already made sure the TCS is not in use: this
// driver owns TCS 0 for the whole boot, as the AP is the only RPMh client on the application RSC.
static int tcs_write(volatile uint32_t *tcs_base, const struct rpmh_layout *layout, unsigned tcs,
                     const uint32_t *addresses, const uint32_t *data, unsigned count) {
    volatile uint32_t *block = (volatile uint32_t *)((volatile char *)tcs_base + layout->tcs_stride * tcs);
    uint32_t bit = 1u << tcs, control;

    if (rd32(tcs_base, layout->irq_status) & bit) wr32(tcs_base, layout->irq_clear, bit);
    wr32(block, layout->cmd_enable, 0);
    // The hardware requires the trigger to be cleared before the mode enable.
    control = rd32(block, layout->control) & ~RPMH_TCS_TRIGGER;
    wr32(block, layout->control, control);
    control &= ~RPMH_TCS_ENABLE;
    wr32(block, layout->control, control);
    for (unsigned i = 0; i < count; ++i) {
        wr32(block, layout->cmd_msgid + layout->cmd_stride * i, RPMH_MSGID_WRITE);
        wr32(block, layout->cmd_addr + layout->cmd_stride * i, addresses[i]);
        wr32(block, layout->cmd_data + layout->cmd_stride * i, data[i]);
    }
    wr32(block, layout->wait_for_completion, (1u << count) - 1u);
    wr32(block, layout->cmd_enable, (1u << count) - 1u);
    wr32(block, layout->control, RPMH_TCS_ENABLE);
    wr32(block, layout->control, RPMH_TCS_ENABLE | RPMH_TCS_TRIGGER);
    return 0;
}

// Releases the TCS the way the interrupt handler does, without an interrupt of our own: the
// completion is polled, then the trigger, the mode enable and the command enables are cleared.
static void tcs_reclaim(volatile uint32_t *tcs_base, const struct rpmh_layout *layout, unsigned tcs) {
    volatile uint32_t *block = (volatile uint32_t *)((volatile char *)tcs_base + layout->tcs_stride * tcs);
    uint32_t control = rd32(block, layout->control) & ~RPMH_TCS_TRIGGER;
    wr32(block, layout->control, control);
    wr32(block, layout->control, control & ~RPMH_TCS_ENABLE);
    wr32(block, layout->cmd_enable, 0);
    wr32(tcs_base, layout->irq_clear, 1u << tcs);
}

int rpmh_regulator_on(volatile uint32_t *rsc, uint32_t tcs_offset, const void *cmd_db, size_t cmd_db_size,
                      const char *name, unsigned millivolts, rpmh_delay_fn delay, void *context,
                      uint32_t *address, uint32_t *irq_seen) {
    struct rpmh_layout layout;
    volatile uint32_t *tcs_base;
    uint32_t found = 0, seen = 0;
    uint32_t addresses[RPMH_MAX_CMDS], data[RPMH_MAX_CMDS] = {0, RPMH_REGULATOR_ON, RPMH_REGULATOR_MODE_LDO_HPM};
    int rc;

    if (!rsc || !cmd_db || !name || !delay) return RPMH_EINVAL;
    rc = rpmh_layout_for(rd32(rsc, 0), &layout);
    if (rc) return rc;
    rc = rpmh_address(cmd_db, cmd_db_size, name, &found);
    if (rc) return rc;
    if (!found || ((found >> 16) & 0xfu) != RPMH_REGULATOR_TYPE) return RPMH_EBADADDR;
    if (address) *address = found;
    tcs_base = (volatile uint32_t *)((volatile char *)rsc + tcs_offset);
    data[0] = millivolts;
    for (unsigned i = 0; i < RPMH_MAX_CMDS; ++i) addresses[i] = found + 4 * i;
    rc = tcs_write(tcs_base, &layout, 0, addresses, data, RPMH_MAX_CMDS);
    if (rc) return rc;
    for (unsigned i = 0; i < RPMH_POLL_MS; ++i) {
        seen = rd32(tcs_base, layout.irq_status);
        if (seen & 1u) break;
        delay(context, 1000);
    }
    tcs_reclaim(tcs_base, &layout, 0);
    if (irq_seen) *irq_seen = seen;
    return (seen & 1u) ? 0 : RPMH_ETIMEOUT;
}
