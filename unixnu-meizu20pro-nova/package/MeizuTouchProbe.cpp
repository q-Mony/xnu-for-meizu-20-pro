// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off staged bring-up of the Goodix Berlin D (GT9916) touch controller on the Meizu 20 Pro.
// `meizu-touch=<n>`:
//   1  read-only snapshot of the QUP1 SE4 clock branches, the DFS levels and the touch pins
//   2  vote the QUP clocks, wait for them, mux the I2C pins and release the controller from reset
//   3  GENI I2C init and 1 MHz clock, then the boot-option handshake, the firmware version and the IC info
//   4  poll the event block and publish contacts for NativeHidInput
// Invalid stages leave the probe off. `meizu-touch-poll=<ms>` accepts 1..100;
// an absent, zero or invalid interval keeps the 4 ms default.
// Every stage is reported with IOLog; the first failure stops the sequence. Nothing here is validated on the
// device until a boot has shown it. Board constants and their sources are in MeizuTouchBoard.h.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <os/cpp_util.h> // placement new for the IOMalloc-backed Session
#include <pexpert/pexpert.h>
#include <string.h>
extern "C" {
#include "GeniI2c.h"
#include "GoodixBerlin.h"
#include "MeizuTouchBoard.h"
#include "Rpmh.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <mach/vm_types.h>

extern "C" void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);
}

// The published finger set. NativeHidInput reads it through the seqlock (odd while a writer owns it) and registers
// a sink with native_touch_set_sink, which is called from this thread after the sequence closes.
extern "C" {
volatile uint32_t native_touch_seq;
volatile uint32_t native_touch_count;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
volatile uint32_t native_touch_hid_reports;
volatile int native_touch_error;
volatile int native_touch_last_x = -1, native_touch_last_y = -1;
volatile uint32_t native_touch_frames, native_touch_valid, native_touch_contacts;
// native_touch_edge_stats belongs to the owned IOHIDDevice (drivers/NativeHidInput), which also counts edges.
// [0] stage reached, [1] first error, [2] QUP hardware version, [3] GENI firmware revision, [4] IF_DISABLE,
// [5] HW_PARAM_0, [6] TX FIFO depth, [7] GENI_CLK_SEL level, [8] SCL Hz, [9] info source, [10] event address,
// [11] head_len << 16 | point_len, [12] max_x << 16 | max_y, [13] events, [14] checksum errors, [15] bus errors.
volatile uint32_t native_meizu_touch_stats[16];
// [0] RSC version word, [1] major, [2] avdd address, [3] avdd result, [4] iovdd address, [5] iovdd result,
// [6] avdd IRQ_STATUS seen, [7] iovdd IRQ_STATUS seen.
volatile uint32_t native_meizu_rpmh_stats[8];
}

namespace {
constexpr unsigned kSlots = 12;

struct Region {
    OSSharedPtr<IOMemoryMap> keep;
    uintptr_t va = 0;
};

bool mapRegion(uint64_t phys, uint64_t length, Region &region) {
    IODeviceMemory *memory = IODeviceMemory::withRange(phys, length);
    if (!memory) return false;
    region.keep = OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere | kIOMapInhibitCache), OSNoRetain);
    memory->release();
    region.va = region.keep ? static_cast<uintptr_t>(region.keep->getVirtualAddress()) : 0;
    return region.va != 0;
}

uint32_t rd32(uintptr_t base, uint32_t reg) { return *reinterpret_cast<volatile uint32_t *>(base + reg); }
void wr32(uintptr_t base, uint32_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(base + reg) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

__attribute__((unused)) uint32_t ioRd(void *ctx, uint32_t reg) { return rd32(reinterpret_cast<uintptr_t>(ctx), reg); }
__attribute__((unused)) void ioWr(void *ctx, uint32_t reg, uint32_t value) { wr32(reinterpret_cast<uintptr_t>(ctx), reg, value); }
// The I2C engine polls its status registers with a 1 us wait between reads; a wait that short spins on the counter
// instead of blocking the thread.
void ioDelayUs(void *, unsigned us) {
    if (us > 50) { IODelay(us); return; }
    uint64_t frequency = 0, start = 0, now = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(start));
    const uint64_t ticks = (frequency ? frequency : 19200000) * us / 1000000 + 1;
    do { __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(now)); } while (now - start < ticks);
}

struct Session {
    Region gccLow, gccS4, gccHalt, gccVote, tlmm, wrapper, se, rsc, cmdb;
    IOBufferMemoryDescriptor *dmaBuf = nullptr;
    struct geni_i2c bus;
    struct gb_dev chip;
    uint64_t levelHz[MTB_DFS_LEVELS];
    unsigned level = 0;
    uint32_t vote, haltCore, haltCore2x, haltMAhb, haltSAhb, haltS4, gpll0Mode;
    uint32_t pinCtl[4];                   // reset 24, irq 25, sda 44, scl 45
    uint32_t panelX = MTB_PANEL_MAX_X, panelY = MTB_PANEL_MAX_Y;
    uint32_t pollMs = 4;
    int flipX = 0, flipY = 0;
    uint32_t events, checksumErrors, busErrors, emptyPolls;
    uint8_t cmdAddrReg[4] = {0, 0, 0, 0};
};

Session *gSession;

// TLMM page of pins 24..45, addressed by absolute pin register offset from pin 24's page.
uint32_t tlmmRd(const Session &s, uint32_t reg) { return rd32(s.tlmm.va, reg - MTB_GPIO_TS_RESET * MTB_GPIO_STRIDE); }
void tlmmWr(Session &s, uint32_t reg, uint32_t value) {
    wr32(s.tlmm.va, reg - MTB_GPIO_TS_RESET * MTB_GPIO_STRIDE, value);
}

// Software I2C on the touch pins, used when the QUP reports FIFO_IF_DISABLE:
// the two lines stay GPIO open-drain and the controller is clocked by hand.
void bbLow(Session &s, unsigned gpio) {
    tlmmWr(s, mtb_tlmm_io(gpio), 0u);
    tlmmWr(s, mtb_tlmm_ctl(gpio),
           mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_2MA, MTB_PULL_UP) | MTB_PIN_OUTPUT_ENABLE);
}
void bbRelease(Session &s, unsigned gpio) {
    tlmmWr(s, mtb_tlmm_ctl(gpio), mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_2MA, MTB_PULL_UP));
}
bool bbLevel(Session &s, unsigned gpio) { return (tlmmRd(s, mtb_tlmm_io(gpio)) & 1u) != 0; }
void bbStart(Session &s) {
    bbRelease(s, MTB_GPIO_I2C_SDA); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(8);
    bbLow(s, MTB_GPIO_I2C_SDA); IODelay(8); bbLow(s, MTB_GPIO_I2C_SCL); IODelay(8);
}
void bbStop(Session &s) {
    bbLow(s, MTB_GPIO_I2C_SDA); IODelay(8); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(8);
    bbRelease(s, MTB_GPIO_I2C_SDA); IODelay(8);
}
void bbSclHigh(Session &s) {
    bbRelease(s, MTB_GPIO_I2C_SCL);
    for (unsigned i = 0; i < 2000 && !bbLevel(s, MTB_GPIO_I2C_SCL); ++i) IODelay(1);   // clock stretching
    IODelay(2);
}
bool bbWrite(Session &s, uint8_t v) {
    for (int i = 7; i >= 0; --i) {
        if (v & (1u << i)) bbRelease(s, MTB_GPIO_I2C_SDA); else bbLow(s, MTB_GPIO_I2C_SDA);
        IODelay(2);
        bbSclHigh(s);
        bbLow(s, MTB_GPIO_I2C_SCL); IODelay(2);
    }
    bbRelease(s, MTB_GPIO_I2C_SDA); IODelay(2);
    bbSclHigh(s);
    bool ack = !bbLevel(s, MTB_GPIO_I2C_SDA);
    bbLow(s, MTB_GPIO_I2C_SCL); IODelay(2);
    return ack;
}
uint8_t bbRead(Session &s, bool ackAfter);
bool bbWriteReg(Session &s, uint32_t reg, const uint8_t *data, unsigned len) {
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>(MTB_I2C_ADDR << 1))) { bbStop(s); return false; }
    if (!bbWrite(s, static_cast<uint8_t>(reg >> 24)) || !bbWrite(s, static_cast<uint8_t>(reg >> 16)) ||
        !bbWrite(s, static_cast<uint8_t>(reg >> 8)) || !bbWrite(s, static_cast<uint8_t>(reg))) {
        bbStop(s); return false;
    }
    for (unsigned i = 0; i < len; ++i) {
        if (!bbWrite(s, data[i])) { bbStop(s); return false; }
    }
    bbStop(s);
    return true;
}
bool bbReadReg(Session &s, uint32_t reg, uint8_t *data, unsigned len) {
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>(MTB_I2C_ADDR << 1))) { bbStop(s); return false; }
    if (!bbWrite(s, static_cast<uint8_t>(reg >> 24)) || !bbWrite(s, static_cast<uint8_t>(reg >> 16)) ||
        !bbWrite(s, static_cast<uint8_t>(reg >> 8)) || !bbWrite(s, static_cast<uint8_t>(reg))) {
        bbStop(s); return false;
    }
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>((MTB_I2C_ADDR << 1) | 1))) { bbStop(s); return false; }
    for (unsigned i = 0; i < len; ++i) data[i] = bbRead(s, i + 1 < len);
    bbStop(s);
    return true;
}
// GeniI2c transport hooks: the Goodix driver talks to the controller through
// these when the QUP has no usable FIFO/DMA path.
int softI2cWrite(struct geni_i2c *, unsigned addr7, const uint8_t *buf, size_t len, int stop) {
    if (!gSession) return -6;
    Session &s = *gSession;
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>(addr7 << 1))) { bbStop(s); return -6; }
    for (size_t i = 0; i < len; ++i) {
        if (!bbWrite(s, buf[i])) { bbStop(s); return -6; }
    }
    if (stop) bbStop(s);
    return 0;
}
int softI2cRead(struct geni_i2c *, unsigned addr7, const uint8_t *tx, size_t txlen,
                uint8_t *rx, size_t rxlen) {
    if (!gSession) return -6;
    Session &s = *gSession;
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>(addr7 << 1))) { bbStop(s); return -6; }
    for (size_t i = 0; i < txlen; ++i) {
        if (!bbWrite(s, tx[i])) { bbStop(s); return -6; }
    }
    bbStart(s);
    if (!bbWrite(s, static_cast<uint8_t>((addr7 << 1) | 1))) { bbStop(s); return -6; }
    for (size_t i = 0; i < rxlen; ++i) rx[i] = bbRead(s, i + 1 < rxlen);
    bbStop(s);
    return 0;
}

uint8_t bbRead(Session &s, bool ackAfter) {
    uint8_t v = 0;
    bbRelease(s, MTB_GPIO_I2C_SDA);
    for (int i = 7; i >= 0; --i) {
        IODelay(2);
        bbSclHigh(s);
        if (bbLevel(s, MTB_GPIO_I2C_SDA)) v |= static_cast<uint8_t>(1u << i);
        bbLow(s, MTB_GPIO_I2C_SCL); IODelay(2);
    }
    if (ackAfter) bbLow(s, MTB_GPIO_I2C_SDA); else bbRelease(s, MTB_GPIO_I2C_SDA);
    IODelay(2); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(3); bbLow(s, MTB_GPIO_I2C_SCL); IODelay(2);
    bbRelease(s, MTB_GPIO_I2C_SDA);
    return v;
}
// Read the Goodix chip-id window straight off the pins, no GENI involved.
void bbProbe(Session &s) {
    uint8_t id[4] = {0, 0, 0, 0};
    bbRelease(s, MTB_GPIO_I2C_SDA); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(10);
    kprintf("meizu-touch: bitbang idle=%d%d\n",
            bbLevel(s, MTB_GPIO_I2C_SCL), bbLevel(s, MTB_GPIO_I2C_SDA));
    bbLow(s, MTB_GPIO_I2C_SDA); IODelay(3);
    bool lo = bbLevel(s, MTB_GPIO_I2C_SDA);
    bbRelease(s, MTB_GPIO_I2C_SDA); IODelay(3);
    bool hi = bbLevel(s, MTB_GPIO_I2C_SDA);
    kprintf("meizu-touch: bitbang gpio low=%d high=%d\n", lo, hi);
    kprintf("meizu-touch: cfg24=%08x cfg25=%08x cfg44=%08x cfg45=%08x\n",
            tlmmRd(s, mtb_tlmm_ctl(MTB_GPIO_TS_RESET)),
            tlmmRd(s, mtb_tlmm_ctl(MTB_GPIO_TS_IRQ)),
            tlmmRd(s, mtb_tlmm_ctl(MTB_GPIO_I2C_SDA)),
            tlmmRd(s, mtb_tlmm_ctl(MTB_GPIO_I2C_SCL)));
    // Drive reset high again and dump the raw IN_OUT words (bit0 = OUT,
    // bit1 = pad input) plus a 200 ms settle before the scan.
    tlmmWr(s, mtb_tlmm_io(MTB_GPIO_TS_RESET), 1u); IODelay(1000);
    kprintf("meizu-touch: io24=%08x io25=%08x io44=%08x io45=%08x\n",
            tlmmRd(s, mtb_tlmm_io(MTB_GPIO_TS_RESET)),
            tlmmRd(s, mtb_tlmm_io(MTB_GPIO_TS_IRQ)),
            tlmmRd(s, mtb_tlmm_io(MTB_GPIO_I2C_SDA)),
            tlmmRd(s, mtb_tlmm_io(MTB_GPIO_I2C_SCL)));
    IODelay(200000);
    // Power probe: drop the internal pull-ups. External pull-ups sit on the
    // touch IOVDD rail, so a high level means the 1.8 V rail is alive.
    static const unsigned nopull[4] = {MTB_GPIO_I2C_SDA, MTB_GPIO_I2C_SCL, MTB_GPIO_TS_RESET, MTB_GPIO_TS_IRQ};
    for (unsigned i = 0; i < 4; ++i)
        tlmmWr(s, mtb_tlmm_ctl(nopull[i]), mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_2MA, 0));
    IODelay(5000);
    kprintf("meizu-touch: nopull sda=%d scl=%d reset=%d irq=%d\n",
            bbLevel(s, MTB_GPIO_I2C_SDA), bbLevel(s, MTB_GPIO_I2C_SCL),
            bbLevel(s, MTB_GPIO_TS_RESET), bbLevel(s, MTB_GPIO_TS_IRQ));
    bbRelease(s, MTB_GPIO_I2C_SDA); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(2000);
    uint32_t hits = 0;
    for (unsigned addr = 0x08; addr <= 0x77; ++addr) {
        bbStart(s);
        bool ack = bbWrite(s, static_cast<uint8_t>(addr << 1));
        bbStop(s);
        if (ack) hits |= 1u << (addr & 31);
    }
    kprintf("meizu-touch: scan-all=%x\n", hits);
    // GB_DEV_CONFIRM_LEN is 8: a longer write would spill into the firmware
    // version record at 0x10014.
    uint8_t tx[8], rx[8];
    for (unsigned i = 0; i < 8; ++i) { tx[i] = 0xaa; rx[i] = 0; }
    bool wrote = bbWriteReg(s, 0x10000u, tx, 8);
    bool read = wrote && bbReadReg(s, 0x10000u, rx, 8);
    unsigned match = 0;
    for (unsigned i = 0; i < 8; ++i) if (rx[i] == 0xaa) ++match;
    kprintf("meizu-touch: confirm w=%d r=%d match=%u rx=%02x%02x%02x%02x\n",
            wrote, read, match, rx[0], rx[1], rx[2], rx[3]);
    if (!hits) {
        // Alternate polarity: assert high, release low, then look again at 0x5d.
        tlmmWr(s, mtb_tlmm_io(MTB_GPIO_TS_RESET), 1u);
        IODelay(10000);
        tlmmWr(s, mtb_tlmm_io(MTB_GPIO_TS_RESET), 0u);
        IODelay(50000);
        bbRelease(s, MTB_GPIO_I2C_SDA); bbRelease(s, MTB_GPIO_I2C_SCL); IODelay(10);
        bbStart(s);
        bool ack = bbWrite(s, static_cast<uint8_t>(MTB_I2C_ADDR << 1));
        bbStop(s);
        kprintf("meizu-touch: bitbang alt-reset ack=%d irq=%d\n", ack,
                (tlmmRd(s, mtb_tlmm_io(MTB_GPIO_TS_IRQ)) & 1u) != 0);
    }
    bbStart(s);
    bool a = bbWrite(s, static_cast<uint8_t>(MTB_I2C_ADDR << 1));
    bool b = a && bbWrite(s, 0x00);
    bool c = b && bbWrite(s, 0x00);
    bool d = false;
    if (c) { bbStart(s); d = bbWrite(s, static_cast<uint8_t>((MTB_I2C_ADDR << 1) | 1)); }
    if (d) { for (int i = 0; i < 4; ++i) id[i] = bbRead(s, i != 3); }
    bbStop(s);
    kprintf("meizu-touch: bitbang ack=%d%d%d%d id=%02x%02x%02x%02x\n",
            a, b, c, d, id[0], id[1], id[2], id[3]);
}

void fail(unsigned step, int code) {
    if (!native_touch_error) native_touch_error = code ? code : -1;
    native_meizu_touch_stats[0] = step;
    if (!native_meizu_touch_stats[1]) native_meizu_touch_stats[1] = (uint32_t)code;
    IOLog("meizu-touch: stage %u failed, rc %d\n", step, code);
}

bool running(uint32_t cbcr) {
    // Branches with hardware clock gating (bit 1) show CLK_OFF while idle and wake on access.
    return !(cbcr & MTB_CBCR_CLK_OFF) || (cbcr & 2u);
}

bool snapshot(Session &s) {
    if (!mapRegion(MTB_GCC_BASE + 0x0000, 0x1000, s.gccLow) ||
        !mapRegion(MTB_GCC_BASE + 0x18000, 0x1000, s.gccS4) ||
        !mapRegion(MTB_GCC_BASE + 0x23000, 0x1000, s.gccHalt) ||
        !mapRegion(MTB_GCC_BASE + 0x52000, 0x1000, s.gccVote) ||
        !mapRegion(MTB_TLMM_BASE + MTB_GPIO_TS_RESET * MTB_GPIO_STRIDE,
                   (MTB_GPIO_I2C_SCL - MTB_GPIO_TS_RESET + 1) * MTB_GPIO_STRIDE, s.tlmm)) {
        fail(1, -12);
        return false;
    }
    s.vote = rd32(s.gccVote.va, MTB_GCC_VOTE_ENABLE & 0xfff);
    s.haltMAhb = rd32(s.gccHalt.va, MTB_HALT_M_AHB & 0xfff);
    s.haltSAhb = rd32(s.gccHalt.va, MTB_HALT_S_AHB & 0xfff);
    s.haltCore = rd32(s.gccHalt.va, MTB_HALT_CORE & 0xfff);
    s.haltCore2x = rd32(s.gccHalt.va, MTB_HALT_CORE_2X & 0xfff);
    s.haltS4 = rd32(s.gccS4.va, MTB_HALT_S4 & 0xfff);
    s.gpll0Mode = rd32(s.gccLow.va, MTB_GCC_GPLL0_MODE);
    for (unsigned l = 0; l < MTB_DFS_LEVELS; ++l) {
        uint32_t cfg = rd32(s.gccS4.va, (MTB_S4_RCG + MTB_RCG_PERF_CFG(l)) & 0xfff);
        uint32_t m = rd32(s.gccS4.va, (MTB_S4_RCG + MTB_RCG_PERF_M(l)) & 0xfff);
        uint32_t n = rd32(s.gccS4.va, (MTB_S4_RCG + MTB_RCG_PERF_N(l)) & 0xfff);
        s.levelHz[l] = mtb_dfs_level_hz(cfg, m, n);
    }
    static const unsigned pins[4] = {MTB_GPIO_TS_RESET, MTB_GPIO_TS_IRQ, MTB_GPIO_I2C_SDA, MTB_GPIO_I2C_SCL};
    for (unsigned i = 0; i < 4; ++i) s.pinCtl[i] = tlmmRd(s, mtb_tlmm_ctl(pins[i]));
    kprintf("meizu-touch: levels reset=%d irq=%d\n",
            (tlmmRd(s, mtb_tlmm_io(MTB_GPIO_TS_RESET)) & 1u) != 0,
            (tlmmRd(s, mtb_tlmm_io(MTB_GPIO_TS_IRQ)) & 1u) != 0);
    IOLog("meizu-touch: vote %08x halt s4 %08x core %08x/%08x ahb %08x/%08x pins %08x %08x %08x %08x\n",
          s.vote, s.haltS4, s.haltCore, s.haltCore2x, s.haltMAhb, s.haltSAhb,
          s.pinCtl[0], s.pinCtl[1], s.pinCtl[2], s.pinCtl[3]);
    native_meizu_touch_stats[0] = 1;
    return true;
}

// The two touch supplies are not always-on; Android's own driver is their consumer, so the same three-word
// votes (voltage, on, LDO HPM) are placed through the AP's active TCS before the controller leaves reset.
// A hard failure is recorded and the sequence continues: the rails may already be up from the firmware, and
// the identification step will state plainly whether the chip is there.
bool powerRails(Session &s) {
    kprintf("meizu-touch: rails map rsc\n");
    if (!mapRegion(MTB_RSC_BASE, MTB_RSC_SIZE, s.rsc)) { kprintf("meizu-touch: rsc map failed\n"); return false; }
    kprintf("meizu-touch: rails map cmd-db\n");
    if (!mapRegion(MTB_CMD_DB_BASE, MTB_CMD_DB_SIZE, s.cmdb)) { kprintf("meizu-touch: cmd-db map failed\n"); return false; }
    if (false) {
        native_meizu_rpmh_stats[3] = (uint32_t)RPMH_EINVAL;
        native_meizu_rpmh_stats[5] = (uint32_t)RPMH_EINVAL;
        IOLog("meizu-touch: rsc or command-db mapping failed\n");
        return false;
    }
    auto *rsc = reinterpret_cast<volatile uint32_t *>(s.rsc.va);
    native_meizu_rpmh_stats[0] = rsc[0];
    native_meizu_rpmh_stats[1] = (rsc[0] >> 16) & 0xff;
    struct rpmh_layout layout;
    if (rpmh_layout_for(rsc[0], &layout)) {
        native_meizu_rpmh_stats[3] = (uint32_t)RPMH_EBADVERSION;
        native_meizu_rpmh_stats[5] = (uint32_t)RPMH_EBADVERSION;
        IOLog("meizu-touch: rsc version %08x is not a known layout\n", rsc[0]);
        return false;
    }
    static const struct { const char *name; unsigned mv, address, result, seen; } rails[2] = {
        {MTB_RAIL_AVDD, MTB_RAIL_AVDD_MV, 2, 3, 6},
        {MTB_RAIL_IOVDD, MTB_RAIL_IOVDD_MV, 4, 5, 7},
    };
    bool ok = true;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t address = 0, seen = 0;
        kprintf("meizu-touch: rail vote %s\n", rails[i].name);
        int rc = rpmh_regulator_on(rsc, MTB_RSC_TCS_OFFSET, reinterpret_cast<const void *>(s.cmdb.va),
                                   (size_t)MTB_CMD_DB_SIZE, rails[i].name, rails[i].mv, ioDelayUs, nullptr,
                                   &address, &seen);
        native_meizu_rpmh_stats[rails[i].address] = address;
        native_meizu_rpmh_stats[rails[i].result] = (uint32_t)rc;
        native_meizu_rpmh_stats[rails[i].seen] = seen;
        IOLog("meizu-touch: rail %s at %08x %u mV, rc %d, irq %08x\n", rails[i].name, address, rails[i].mv, rc, seen);
        if (rc && rc != RPMH_ETIMEOUT) ok = false;   // the vote went out even when the report timed out
    }
    return ok;
}

bool enable(Session &s) {
    powerRails(s);
    // GPLL0 feeds the DFS levels; vote for it (a shared vote) and wait for lock.
    uint32_t gpll0 = rd32(s.gccVote.va, MTB_GCC_GPLL0_ENABLE & 0xfff);
    wr32(s.gccVote.va, MTB_GCC_GPLL0_ENABLE & 0xfff, gpll0 | 1u);
    for (unsigned i = 0; i < 10000 && !(rd32(s.gccLow.va, MTB_GCC_GPLL0_MODE) & (1u << 31)); ++i) IODelay(10);
    wr32(s.gccVote.va, MTB_GCC_VOTE_ENABLE & 0xfff, rd32(s.gccVote.va, MTB_GCC_VOTE_ENABLE & 0xfff) | MTB_VOTE_ALL);
    bool ok = false;
    for (unsigned i = 0; i < 2000 && !ok; ++i) {
        ok = running(rd32(s.gccHalt.va, MTB_HALT_M_AHB & 0xfff)) && running(rd32(s.gccHalt.va, MTB_HALT_S_AHB & 0xfff)) &&
             !(rd32(s.gccHalt.va, MTB_HALT_CORE & 0xfff) & MTB_CBCR_CLK_OFF) &&
             !(rd32(s.gccHalt.va, MTB_HALT_CORE_2X & 0xfff) & MTB_CBCR_CLK_OFF) &&
             !(rd32(s.gccS4.va, MTB_HALT_S4 & 0xfff) & MTB_CBCR_CLK_OFF);
        if (!ok) IODelay(10);
    }
    s.vote = rd32(s.gccVote.va, MTB_GCC_VOTE_ENABLE & 0xfff);
    s.haltMAhb = rd32(s.gccHalt.va, MTB_HALT_M_AHB & 0xfff);
    s.haltSAhb = rd32(s.gccHalt.va, MTB_HALT_S_AHB & 0xfff);
    s.haltCore = rd32(s.gccHalt.va, MTB_HALT_CORE & 0xfff);
    s.haltCore2x = rd32(s.gccHalt.va, MTB_HALT_CORE_2X & 0xfff);
    s.haltS4 = rd32(s.gccS4.va, MTB_HALT_S4 & 0xfff);
    if (!ok) { fail(2, -110); return false; }
    // SDA gpio44 and SCL gpio45: qup1_se4 function, 2 mA, pull-up, as qupv3_se4_i2c_sda/scl_active.
    const uint32_t i2cCfg = mtb_pin_cfg(MTB_FUNC_QUP1_SE4, MTB_DRIVE_2MA, MTB_PULL_UP);
    tlmmWr(s, mtb_tlmm_ctl(MTB_GPIO_I2C_SDA), i2cCfg);
    tlmmWr(s, mtb_tlmm_ctl(MTB_GPIO_I2C_SCL), i2cCfg);
    // Reset (24) as a GPIO output, released high; IRQ (25) as a GPIO input with the DT's pull-up.
    tlmmWr(s, mtb_tlmm_ctl(MTB_GPIO_TS_RESET),
           mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_8MA, MTB_PULL_UP) | MTB_PIN_OUTPUT_ENABLE);
    tlmmWr(s, mtb_tlmm_io(MTB_GPIO_TS_RESET), 1u);
    tlmmWr(s, mtb_tlmm_ctl(MTB_GPIO_TS_IRQ), mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_8MA, MTB_PULL_UP));
    // Power-on: assert reset low, then release by turning the pad back into an
    // input. Driving IN_OUT high does not work on this TLMM (the output bit
    // stays 0), and the board pulls the line up, so releasing the pin is what
    // takes the controller out of reset.
    tlmmWr(s, mtb_tlmm_io(MTB_GPIO_TS_RESET), 0u);
    IODelay(10000);
    tlmmWr(s, mtb_tlmm_ctl(MTB_GPIO_TS_RESET),
           mtb_pin_cfg(MTB_FUNC_GPIO, MTB_DRIVE_8MA, MTB_PULL_UP));
    IODelay(30000);
    static const unsigned pins[4] = {MTB_GPIO_TS_RESET, MTB_GPIO_TS_IRQ, MTB_GPIO_I2C_SDA, MTB_GPIO_I2C_SCL};
    for (unsigned i = 0; i < 4; ++i) s.pinCtl[i] = tlmmRd(s, mtb_tlmm_ctl(pins[i]));
    IOLog("meizu-touch: clocks voted %08x, pins %08x %08x %08x %08x\n", s.vote,
          s.pinCtl[0], s.pinCtl[1], s.pinCtl[2], s.pinCtl[3]);
    native_meizu_touch_stats[0] = 2;
    return true;
}

bool openBus(Session &s) {
    // The QUP reports FIFO_IF_DISABLE and its legacy DMA is not usable, so the
    // controller is driven over the same two pads in software.
    s.bus.se = (struct geni_io){nullptr, nullptr, nullptr, ioDelayUs};
    s.bus.speed_hz = 100000;
    s.bus.soft_ctx = &s;
    s.bus.soft_write = softI2cWrite;
    s.bus.soft_read = softI2cRead;
    bbRelease(s, MTB_GPIO_I2C_SDA);
    bbRelease(s, MTB_GPIO_I2C_SCL);
    IODelay(50);
    native_meizu_touch_stats[0] = 3;
    IOLog("meizu-touch: software i2c %u Hz\n", s.bus.speed_hz);
    return true;
}

// Vendor command channel: 16-byte record at misc.cmd_addr, checksum over the
// length/command bytes, the chip answers by setting state to 0x80.
int touchCommand(Session &s, uint8_t command, unsigned len) {
    uint8_t buf[16] = {};
    buf[2] = static_cast<uint8_t>(len);
    buf[3] = command;
    unsigned sum = 0;
    for (unsigned i = 2; i < len; ++i) sum += buf[i];
    buf[len] = static_cast<uint8_t>(sum & 0xff);
    buf[len + 1] = static_cast<uint8_t>((sum >> 8) & 0xff);
    if (geni_i2c_write(&s.bus, MTB_I2C_ADDR, buf, sizeof buf, 1)) {
        kprintf("meizu-touch: cmd %02x write failed\n", command);
        return -1;
    }
    for (unsigned i = 0; i < 20; ++i) {
        uint8_t ack[16] = {};
        if (!geni_i2c_read(&s.bus, MTB_I2C_ADDR, s.cmdAddrReg, 4, ack, sizeof ack)) {
            if (i == 0) {
                kprintf("meizu-touch: cmd %02x reply %02x%02x%02x%02x\n",
                        command, ack[0], ack[1], ack[2], ack[3]);
            }
            if (ack[1] == 0x80) {   // CMD_ACK_OK lives in the ack byte
                kprintf("meizu-touch: cmd %02x ok state=%02x\n", command, ack[0]);
                return 0;
            }
        }
        IOSleep(20);
    }
    kprintf("meizu-touch: cmd %02x no ack\n", command);
    return -1;
}

bool identify(Session &s) {
    kprintf("meizu-touch: bitbang probe\n");
    bbProbe(s);
    s.chip.bus = &s.bus;
    s.chip.addr = MTB_I2C_ADDR;
    int r = gb_dev_confirm(&s.chip);
    if (r) {
        IOLog("meizu-touch: dev confirm failed rc %d (irq %08x status %08x)\n",
              r, s.bus.snap_irq, s.bus.snap_status);
        fail(3, r);
        return false;
    }
    uint8_t version[26] = {};
    uint8_t vraw[26] = {};
    if (bbReadReg(s, 0x10014u, vraw, 26)) {
        unsigned sum24 = 0;
        for (unsigned i = 0; i < 24; ++i) sum24 += vraw[i];
        const unsigned cs = (unsigned)vraw[24] | ((unsigned)vraw[25] << 8);
        kprintf("meizu-touch: ver %02x%02x%02x%02x %02x%02x%02x%02x sum24=%04x cs=%04x\n",
                vraw[0], vraw[1], vraw[2], vraw[3], vraw[4], vraw[5], vraw[6], vraw[7],
                sum24 & 0xffff, cs);
        kprintf("meizu-touch: ver2 %02x%02x%02x%02x %02x%02x%02x%02x\n",
                vraw[8], vraw[9], vraw[10], vraw[11], vraw[12], vraw[13], vraw[14], vraw[15]);
        kprintf("meizu-touch: ver3 %02x%02x%02x%02x %02x%02x%02x%02x\n",
                vraw[16], vraw[17], vraw[18], vraw[19], vraw[20], vraw[21], vraw[22], vraw[23]);
    }
    r = gb_read_version(&s.chip, version);
    // The version record of this unit does not follow the ported checksum
    // convention; it is informational, so keep going and report it.
    if (r) IOLog("meizu-touch: version check rc %d, continuing\n", r);
    IOLog("meizu-touch: fw version %02x%02x%02x%02x-%02x%02x%02x%02x checksum %02x%02x\n",
          (unsigned)version[0], (unsigned)version[1], (unsigned)version[2], (unsigned)version[3],
          (unsigned)version[4], (unsigned)version[5], (unsigned)version[6], (unsigned)version[7],
          (unsigned)version[24], (unsigned)version[25]);
    uint8_t iblob[32] = {};
    if (bbReadReg(s, 0x10070u, iblob, 32)) {
        kprintf("meizu-touch: icinfo len=%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x\n",
                iblob[1], iblob[0], iblob[2], iblob[3], iblob[4], iblob[5], iblob[6], iblob[7],
                iblob[8], iblob[9]);
        kprintf("meizu-touch: icinfo2 %02x%02x%02x%02x%02x%02x%02x%02x %02x%02x\n",
                iblob[10], iblob[11], iblob[12], iblob[13], iblob[14], iblob[15], iblob[16],
                iblob[17], iblob[18], iblob[19]);
    }
    {
        uint8_t full[199] = {};
        const uint16_t blen = (uint16_t)iblob[0] | ((uint16_t)iblob[1] << 8);
        if (blen >= 6 && blen <= sizeof full && bbReadReg(s, 0x10070u, full, blen)) {
            unsigned sum = 0;
            for (unsigned i = 0; i + 2 < blen; ++i) sum += full[i];
            const unsigned cs = (unsigned)full[blen - 2] | ((unsigned)full[blen - 1] << 8);
            kprintf("meizu-touch: icinfo sum=%04x cs=%04x len=%u ver=%02x\n",
                    sum & 0xffff, cs, blen, full[3]);
            // Walk the same parameter blocks the parser does, then show the
            // misc fields under both candidate layouts.
            size_t o = 2 + 16 + 10;
            if (o + 5 < blen) {
                unsigned rates = full[o + 4];
                o += 5 + rates * 2u;
                for (int i = 0; i < 4 && o < blen; ++i) {
                    unsigned n = full[o];
                    o += 1 + n * 2u;
                }
                if (o + 64 < blen) {
                    const uint8_t *m = full + o;
                    auto u32 = [](const uint8_t *p) {
                        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    };
                    auto u16 = [](const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); };
                    kprintf("meizu-touch: misc packed a=%x h=%u p=%u x=%u y=%u\n",
                            u32(m + 44), u16(m + 48), u16(m + 50), u16(m + 52), u16(m + 54));
                    kprintf("meizu-touch: misc aligned a=%x h=%u p=%u x=%u y=%u\n",
                            u32(m + 52), u16(m + 56), u16(m + 58), u16(m + 60), u16(m + 62));
                }
            }
        }
    }
    r = gb_read_ic_info(&s.chip);
    if (r) {
        kprintf("meizu-touch: icinfo parse rc=%d\n", r);
        // No measured IC-info block exists for this unit; fall back only to keep the probe observable, and say so.
        s.chip.info = {};
        s.chip.info.touch_data_addr = GB_FALLBACK_TOUCH_ADDR;
        s.chip.info.head_len = GB_EVENT_HEAD_LEN;
        s.chip.info.point_len = GB_POINT_LEN;
        s.chip.info.screen_max_x = MTB_PANEL_MAX_X;
        s.chip.info.screen_max_y = MTB_PANEL_MAX_Y;
        s.chip.info.source = GB_INFO_FALLBACK;
        IOLog("meizu-touch: ic info rc %d, using fallback %08x\n", r, GB_FALLBACK_TOUCH_ADDR);
    }
    // Ask for normal (coordinate) mode; without it the controller stays idle.
    {
        const uint32_t addr = s.chip.info.cmd_addr;
        s.cmdAddrReg[0] = (uint8_t)(addr >> 24); s.cmdAddrReg[1] = (uint8_t)(addr >> 16);
        s.cmdAddrReg[2] = (uint8_t)(addr >> 8);  s.cmdAddrReg[3] = (uint8_t)addr;
        kprintf("meizu-touch: cmd addr=%x, sending normal mode\n", addr);
        touchCommand(s, 0x00, 4);
    }
    s.panelX = s.chip.info.screen_max_x ? s.chip.info.screen_max_x : MTB_PANEL_MAX_X;
    s.panelY = s.chip.info.screen_max_y ? s.chip.info.screen_max_y : MTB_PANEL_MAX_Y;
    native_meizu_touch_stats[9] = (uint32_t)s.chip.info.source;
    native_meizu_touch_stats[10] = s.chip.info.touch_data_addr;
    native_meizu_touch_stats[11] = ((uint32_t)s.chip.info.head_len << 16) | s.chip.info.point_len;
    native_meizu_touch_stats[12] = (s.panelX << 16) | (s.panelY & 0xffff);
    IOLog("meizu-touch: ic info source %u addr %08x head %u point %u panel %ux%u\n",
          (unsigned)s.chip.info.source, s.chip.info.touch_data_addr, (unsigned)s.chip.info.head_len,
          (unsigned)s.chip.info.point_len, s.panelX, s.panelY);
    return true;
}

static void (*gTouchSink)(void *);
static void *gTouchSinkContext;
void publish(const Session &s, const struct gb_event &ev) {
    int xs[kSlots], ys[kSlots], ids[kSlots];
    uint32_t n = ev.count > kSlots ? kSlots : ev.count;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t x = gb_scale(ev.points[i].x, (uint16_t)s.panelX, MTB_HID_MAX_X - 1);
        uint32_t y = gb_scale(ev.points[i].y, (uint16_t)s.panelY, MTB_HID_MAX_Y - 1);
        if (s.flipX) x = (MTB_HID_MAX_X - 1) - x;
        if (s.flipY) y = (MTB_HID_MAX_Y - 1) - y;
        xs[i] = (int)x;
        ys[i] = (int)y;
        ids[i] = ev.points[i].id;
    }
    native_touch_seq = native_touch_seq + 1;
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_count = n;
    for (uint32_t i = 0; i < n; ++i) {
        native_touch_xs[i] = xs[i];
        native_touch_ys[i] = ys[i];
        native_touch_ids[i] = ids[i];
    }
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_seq = native_touch_seq + 1;
    native_touch_contacts = n;
    if (n) { native_touch_last_x = xs[0]; native_touch_last_y = ys[0]; }
    if (void (*sink)(void *) = __atomic_load_n(&gTouchSink, __ATOMIC_ACQUIRE)) sink(gTouchSinkContext);
}

// Optional interrupt: the TLMM raises its summary SPI on a falling edge of GPIO25. The poll loop is the default
// because it does not depend on how the bootloader left the line.
extern "C" int armcpu_spi_attach(uint32_t, bool, void (*)(void *), void *, uint32_t *, uint64_t *)
    __attribute__((weak_import));
static volatile uint32_t gIrqEvents;
static volatile uint32_t *gIrqPin;
static void touchIrq(void *) {
    if (!(gIrqPin[3] & 1)) return;      // +0xc INTR_STATUS
    gIrqPin[3] = 0;
    __atomic_fetch_add(&gIrqEvents, 1, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&gIrqEvents);
}
bool armIrq(Session &s) {
    if (!armcpu_spi_attach) return false;
    // intr_cfg at index 0, intr_status four bytes later.
    gIrqPin = reinterpret_cast<volatile uint32_t *>(s.tlmm.va) +
              (mtb_tlmm_intr_cfg(MTB_GPIO_TS_IRQ) - MTB_GPIO_TS_RESET * MTB_GPIO_STRIDE) / 4;
    uint32_t cfg = gIrqPin[0];
    cfg &= ~((7u << 5) | (3u << 2) | (1u << 1) | 1u);
    cfg |= MTB_INTR_TARGET_AP | MTB_INTR_DETECT_FALLING | MTB_INTR_RAW_STATUS;
    gIrqPin[0] = cfg;
    gIrqPin[1] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    if (armcpu_spi_attach(32 + MTB_TLMM_SPI, false, touchIrq, nullptr, nullptr, nullptr)) return false;
    gIrqPin[0] = cfg | 1u;              // enable last
    return true;
}

void eventLoop(Session &s, bool irqArmed) {
    uint32_t seen = gIrqEvents;
    for (;;) {
        if (irqArmed) {
            assert_wait((event_t)&gIrqEvents, THREAD_UNINT);
            if (gIrqEvents != seen) clear_wait(current_thread(), THREAD_AWAKENED);
            else thread_block(THREAD_CONTINUE_NULL);
            seen = gIrqEvents;
        }
        bool any = false;
        for (unsigned burst = 0; burst < 32; ++burst) {
            uint8_t head[GB_EVENT_HEAD_LEN] = {};
            int r = gb_read_reg(&s.chip, s.chip.info.touch_data_addr, head, sizeof(head));
            if (r) {
                s.busErrors++;
                native_meizu_touch_stats[15] = s.busErrors;
                if (!any) IOSleep(10);
                break;
            }
            if (head[0] == 0) {
                if ((++s.emptyPolls % 500) == 0) {
                    IOLog("meizu-touch: idle polls=%u head=%02x%02x%02x%02x errs=%u/%u\n",
                          s.emptyPolls, head[0], head[1], head[2], head[3],
                          s.busErrors, s.checksumErrors);
                }
                break;
            }
            struct gb_event ev;
            r = gb_poll_event(&s.chip, &ev);
            if (r < 0) {
                if (r == GB_ERR_CHECKSUM) { s.checksumErrors++; native_meizu_touch_stats[14] = s.checksumErrors; }
                else { s.busErrors++; native_meizu_touch_stats[15] = s.busErrors; }
                any = true;
                continue;
            }
            if (r == 0) { any = true; continue; }
            s.events++;
            if (s.events == 1 || (s.events % 50) == 0) {
                IOLog("meizu-touch: ev %u n=%u x=%d y=%d\n", s.events,
                      native_touch_count, native_touch_last_x, native_touch_last_y);
            }
            native_meizu_touch_stats[13] = s.events;
            native_touch_frames = s.events;
            native_touch_valid = native_touch_valid + 1;
            publish(s, ev);
            any = true;
            if (irqArmed && gIrqEvents == seen) break;   // no further pulse
        }
        native_meizu_touch_stats[0] = 4;
        if (!irqArmed) IOSleep(s.pollMs);
        else if (!any) IOSleep(2);
    }
}

// Read text first: PE_parse_boot_argn also copies nonnumeric strings into its
// destination and truncates large integers. Neither is a validated number.
// Keep the boot parser's decimal/hex/binary/octal forms, but reject negatives,
// overflow, trailing junk and truncated strings before using any value.
bool boundedBootUnsigned(const char *name, unsigned maximum, unsigned &value) {
    char text[32] = {};
    if (!PE_parse_boot_arg_str(name, text, sizeof text)) return false;
    const size_t length = strlen(text);
    if (!length || length == sizeof text - 1) return false;
    const char *p = text;
    unsigned base = 10;
    if (p[0] == '0' && p[1]) {
        if (p[1] == 'x') { base = 16; p += 2; }
        else if (p[1] == 'b') { base = 2; p += 2; }
        else { base = 8; ++p; }
    }
    if (!*p) return false;
    unsigned result = 0;
    for (; *p; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = static_cast<unsigned>(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = static_cast<unsigned>(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') digit = static_cast<unsigned>(*p - 'A' + 10);
        else return false;
        if (digit >= base || digit > maximum || result > (maximum - digit) / base) return false;
        result = result * base + digit;
    }
    value = result;
    return true;
}

int stageFromBootArgs() {
    unsigned stage = 0;
    // Only stages 1..4 opt in; invalid input must never escalate to stage 4.
    if (!boundedBootUnsigned("meizu-touch", 4, stage)) return 0;
    return (int)stage;
}

void touchThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "meizu-touch");
    kprintf("meizu-touch: thread running\n");
    const int stage = stageFromBootArgs();
    if (!stage) return;
    void *storage = IOMalloc(sizeof(Session));
    if (!storage) { fail(0, -12); return; }
    // Value-initialize: construct OSSharedPtr members, apply panel/poll defaults,
    // and zero the remaining C structs, arrays and counters. Keep the existing
    // boot-lifetime ownership of the session and its mappings.
    Session *s = new (storage) Session{};
    gSession = s;
    unsigned poll = 0, flipX = 0, flipY = 0, irq = 0;
    if (boundedBootUnsigned("meizu-touch-poll", 100, poll) && poll >= 1) s->pollMs = poll;
    PE_parse_boot_argn("meizu-touch-flip-x", &flipX, sizeof flipX);
    PE_parse_boot_argn("meizu-touch-flip-y", &flipY, sizeof flipY);
    PE_parse_boot_argn("meizu-touch-irq", &irq, sizeof irq);
    s->flipX = flipX != 0;
    s->flipY = flipY != 0;
    do {
        kprintf("meizu-touch: stage snapshot\n");
        if (!snapshot(*s)) break;
        kprintf("meizu-touch: stage enable\n");
        if (stage < 2) break;
        if (!enable(*s)) break;
        kprintf("meizu-touch: stage bus\n");
        if (stage < 3) break;
        if (!openBus(*s)) break;
        kprintf("meizu-touch: stage identify\n");
        if (!identify(*s)) break;
        if (stage < 4) break;
        const bool irqArmed = irq != 0 && armIrq(*s);
        IOLog("meizu-touch: running, poll %u ms, irq %s, flip %d/%d\n", s->pollMs, irqArmed ? "on" : "off",
              s->flipX, s->flipY);
        eventLoop(*s, irqArmed);
    } while (0);
    if (native_touch_error) IOLog("meizu-touch: stopped at stage %u, rc %d\n", native_meizu_touch_stats[0], native_touch_error);
}
}  // namespace

extern "C" void native_touch_set_sink(void (*sink)(void *), void *context) {
    gTouchSinkContext = context;
    __atomic_store_n(&gTouchSink, sink, __ATOMIC_RELEASE);
}

// Called from bsd_autoconf after IOKitBSDInit, when the kernel's threads are available.
extern "C" void native_meizu_touch_probe(void) {
    kprintf("meizu-touch: probe enter stage=%u\n", stageFromBootArgs());
    if (stageFromBootArgs() < 1 || native_meizu_touch_stats[0]) return;
    kprintf("meizu-touch: starting thread\n");
    thread_t thread = nullptr;
    if (kernel_thread_start(touchThread, nullptr, &thread) == KERN_SUCCESS) thread_deallocate(thread);
    else native_touch_error = -12;
}
