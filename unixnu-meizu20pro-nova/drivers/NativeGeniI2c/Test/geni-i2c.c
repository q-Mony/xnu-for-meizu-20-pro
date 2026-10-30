// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test: GeniI2c.c against a byte-level model of a GENI serial engine in I2C mode plus a memory-mapped slave.
// The model follows the register behaviour the Linux i2c-qcom-geni driver relies on; it is not hardware. Payload
// order is checked byte for byte, including the 4-byte big-endian register address and the stretch/stop split.
#include "../GeniI2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) do { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); exit(1); } while (0)
#define CHECK(c) do { if (!(c)) FAIL("%s", #c); } while (0)

#define DEPTH 8
#define MEM_SIZE 0x11000

struct sim {
    uint32_t reg[0x2000 / 4];
    uint32_t wrap_ver, hw_param0, fw_rev, if_disable;
    // Engine transfer state.
    unsigned op, addr7;
    int stretch, active, stuck, present, tx_ops, rx_ops;
    size_t tx_rem, rx_rem;
    uint32_t txq[DEPTH]; unsigned txn;
    uint32_t cur_tx_word; unsigned cur_tx_bytes;
    uint32_t rxq[1024]; unsigned rxh, rxt;
    uint32_t cur_rx_word; unsigned cur_rx_bytes;
    unsigned last_valid;
    uint32_t irq, dma_tx_stat, dma_rx_stat;
    int dma_tx_go, dma_rx_go;
    unsigned long cancels, aborts, fsm_rsts;
    size_t seen_n; uint8_t seen[8192];
    // Slave.
    uint8_t mem[MEM_SIZE];
    uint32_t sl_addr; unsigned sl_addr_bytes; int sl_have_addr;
    unsigned long byte_count;
};

static void slave_reset_addr(struct sim *m) { m->sl_addr_bytes = 0; m->sl_have_addr = 0; m->sl_addr = 0; }

static void slave_write_byte(struct sim *m, uint8_t b) {
    if (!m->sl_have_addr) {
        m->sl_addr = (m->sl_addr << 8) | b;
        if (++m->sl_addr_bytes == 4) m->sl_have_addr = 1;
        return;
    }
    m->mem[m->sl_addr++ % MEM_SIZE] = b;
}

static uint8_t slave_read_byte(struct sim *m) {
    if (!m->sl_have_addr) {
        m->sl_addr = (m->sl_addr << 8) | 0xff;
        if (++m->sl_addr_bytes == 4) m->sl_have_addr = 1;
        return 0xff;
    }
    return m->mem[m->sl_addr++ % MEM_SIZE];
}

static void finish_rx_word(struct sim *m) {
    m->rxq[m->rxt++ % 1024] = m->cur_rx_word;
    m->cur_rx_word = 0;
    m->cur_rx_bytes = 0;
}

static void tick(struct sim *m) {
    if (!m->active || m->stuck) return;
    if (m->reg[SE_GENI_DMA_MODE_EN / 4] & GENI_DMA_MODE_EN) return;   // DMA mode: the FIFOs do not shift bytes
    for (int k = 0; k < 8; k++) {
        if (!m->tx_rem && !m->rx_rem) break;
        if (m->tx_rem) {
            if (!m->cur_tx_bytes) {
                if (!m->txn) break;                       // FIFO underrun: firmware must refill
                m->cur_tx_word = m->txq[0];
                for (unsigned i = 1; i < m->txn; i++) m->txq[i - 1] = m->txq[i];
                m->txn--;
                m->cur_tx_bytes = 4;
            }
            uint8_t b = (uint8_t)(m->cur_tx_word & 0xff);
            m->cur_tx_word >>= 8;
            m->cur_tx_bytes--;
            m->seen[m->seen_n++ % sizeof(m->seen)] = b;
            m->byte_count++;
            slave_write_byte(m, b);
            m->tx_rem--;
        }
        if (m->rx_rem) {
            uint8_t b = slave_read_byte(m);
            m->cur_rx_word |= (uint32_t)b << (8 * m->cur_rx_bytes);
            m->cur_rx_bytes++;
            m->byte_count++;
            m->rx_rem--;
            if (m->cur_rx_bytes == 4) finish_rx_word(m);
        }
    }
    if (m->rx_rem == 0 && m->cur_rx_bytes) {
        m->last_valid = m->cur_rx_bytes;
        finish_rx_word(m);
        m->irq |= GENI_M_RX_FIFO_LAST;
    } else if (m->rx_rem == 0 && m->rx_ops && !m->cur_rx_bytes) {
        m->last_valid = 0;
        m->irq |= GENI_M_RX_FIFO_LAST;
    }
    if (m->rx_ops) {
        unsigned words = m->rxt - m->rxh;
        if (words && m->reg[SE_GENI_RX_WATERMARK_REG / 4] && words >= m->reg[SE_GENI_RX_WATERMARK_REG / 4])
            m->irq |= GENI_M_RX_FIFO_WATERMARK;
    }
    if (m->tx_rem && m->reg[SE_GENI_TX_WATERMARK_REG / 4] && m->txn <= m->reg[SE_GENI_TX_WATERMARK_REG / 4])
        m->irq |= GENI_M_TX_FIFO_WATERMARK;
    if (!m->tx_rem && !m->rx_rem) {
        m->irq |= GENI_M_CMD_DONE;
        m->active = 0;
        if (!m->stretch) slave_reset_addr(m);
    }
}

static void start_cmd(struct sim *m, uint32_t v) {
    m->op = v >> GENI_M_OPCODE_SHIFT;
    m->addr7 = (v & GENI_I2C_SLV_ADDR_MASK) >> GENI_I2C_SLV_ADDR_SHIFT;
    m->stretch = (v & GENI_I2C_STOP_STRETCH) != 0;
    CHECK(m->op == GENI_I2C_WRITE || m->op == GENI_I2C_READ);
    CHECK(m->addr7 == 0x5d);
    if (!m->present) {
        m->irq |= GENI_M_GP_IRQ_1 | GENI_M_CMD_DONE;      // address NACK still ends the command
        m->active = 0;
        return;
    }
    m->cur_tx_word = 0; m->cur_tx_bytes = 0;      // the engine drops a partly shifted word between commands
    m->cur_rx_word = 0; m->cur_rx_bytes = 0;
    m->tx_rem = m->op == GENI_I2C_WRITE ? m->reg[SE_I2C_TX_TRANS_LEN / 4] : 0;
    m->rx_rem = m->op == GENI_I2C_READ ? m->reg[SE_I2C_RX_TRANS_LEN / 4] : 0;
    m->tx_ops = m->op == GENI_I2C_WRITE;
    m->rx_ops = m->op == GENI_I2C_READ;
    m->active = 1;
    tick(m);
}

// DMA mode moves bytes between the buffers the pointer registers name; the test passes host addresses.
static void dma_maybe_run(struct sim *m) {
    if (!m->active || !(m->reg[SE_GENI_DMA_MODE_EN / 4] & GENI_DMA_MODE_EN)) return;
    if ((m->tx_rem && !m->dma_tx_go) || (m->rx_rem && !m->dma_rx_go)) return;
    if (m->stuck) return;
    if (!m->present) { m->irq |= GENI_M_GP_IRQ_1 | GENI_M_CMD_DONE; m->active = 0; return; }
    uint8_t *tp = (uint8_t *)(uintptr_t)(((uint64_t)m->reg[SE_DMA_TX_PTR_H / 4] << 32) | m->reg[SE_DMA_TX_PTR_L / 4]);
    uint8_t *rp = (uint8_t *)(uintptr_t)(((uint64_t)m->reg[SE_DMA_RX_PTR_H / 4] << 32) | m->reg[SE_DMA_RX_PTR_L / 4]);
    if (m->tx_rem) {
        CHECK(m->reg[SE_DMA_TX_ATTR / 4] & GENI_DMA_TX_ATTR_EOT_BUF);
        for (size_t i = 0; i < m->tx_rem; i++) {
            m->seen[m->seen_n++ % sizeof(m->seen)] = tp[i];
            slave_write_byte(m, tp[i]);
        }
        m->reg[SE_DMA_TX_LEN_IN / 4] = (uint32_t)m->tx_rem;
        m->dma_tx_stat |= GENI_DMA_DONE | GENI_DMA_EOT;
    }
    if (m->rx_rem) {
        for (size_t i = 0; i < m->rx_rem; i++) rp[i] = slave_read_byte(m);
        m->reg[SE_DMA_RX_LEN_IN / 4] = (uint32_t)m->rx_rem;
        m->dma_rx_stat |= GENI_DMA_DONE;
    }
    m->tx_rem = m->rx_rem = 0;
    m->active = 0;
    if (!m->stretch) slave_reset_addr(m);
}

static uint32_t se_rd(void *ctx, uint32_t r) {
    struct sim *m = ctx;
    switch (r) {
    case GENI_FW_REVISION_RO: return m->fw_rev;
    case GENI_IF_DISABLE_RO: return m->if_disable;
    case SE_HW_PARAM_0: return m->hw_param0;
    case SE_GENI_M_IRQ_STATUS: tick(m); return m->irq;
    case SE_DMA_TX_IRQ_STAT: return m->dma_tx_stat;
    case SE_DMA_RX_IRQ_STAT: return m->dma_rx_stat;
    case SE_GENI_RX_FIFO_STATUS: {
        unsigned words = m->rxt - m->rxh;
        uint32_t v = words & GENI_RX_WC_MASK;
        if ((m->irq & GENI_M_RX_FIFO_LAST) && words <= 1) v |= GENI_RX_LAST | (m->last_valid << GENI_RX_LAST_VALID_SHIFT);
        return v;
    }
    case SE_GENI_RX_FIFOn:
        return m->rxh < m->rxt ? m->rxq[m->rxh++ % 1024] : 0xdeadbeef;
    default: return m->reg[r / 4];
    }
}

static void se_wr(void *ctx, uint32_t r, uint32_t v) {
    struct sim *m = ctx;
    switch (r) {
    case SE_GENI_M_IRQ_CLEAR: m->irq &= ~v; return;
    case SE_DMA_TX_IRQ_CLR: m->dma_tx_stat &= ~v; return;
    case SE_DMA_RX_IRQ_CLR: m->dma_rx_stat &= ~v; return;
    case SE_DMA_TX_FSM_RST: case SE_DMA_RX_FSM_RST: m->fsm_rsts++; return;
    case SE_GENI_TX_FIFOn:
        if (m->txn >= DEPTH) { m->irq |= GENI_M_TX_FIFO_WR_ERR; return; }
        m->txq[m->txn++] = v;
        return;
    case SE_DMA_TX_PTR_L: case SE_DMA_TX_PTR_H: case SE_DMA_TX_ATTR:
        m->reg[r / 4] = v;
        return;
    case SE_DMA_RX_PTR_L: case SE_DMA_RX_PTR_H: case SE_DMA_RX_ATTR:
        m->reg[r / 4] = v;
        return;
    case SE_DMA_TX_LEN: m->reg[r / 4] = v; m->dma_tx_go = 1; dma_maybe_run(m); return;
    case SE_DMA_RX_LEN: m->reg[r / 4] = v; m->dma_rx_go = 1; dma_maybe_run(m); return;
    case SE_GENI_M_CMD_CTRL_REG:
        if (v & GENI_M_CMD_CANCEL) { m->cancels++; m->irq |= GENI_M_CMD_CANCEL_DONE | GENI_M_CMD_DONE; m->active = 0; }
        if (v & GENI_M_CMD_ABORT) { m->aborts++; m->irq |= GENI_M_CMD_ABORT_DONE | GENI_M_CMD_DONE; m->active = 0; }
        return;
    case SE_GENI_M_CMD0:
        m->reg[r / 4] = v;
        start_cmd(m, v);
        return;
    default: m->reg[r / 4] = v; return;
    }
}

static uint32_t wrap_rd(void *ctx, uint32_t r) {
    struct sim *m = ctx;
    CHECK(r == QUPV3_HW_VER_REG);
    return m->wrap_ver;
}

static void no_wr(void *ctx, uint32_t r, uint32_t v) { (void)ctx; (void)r; (void)v; }

static void init_sim(struct sim *m, struct geni_i2c *s, int dma_mode) {
    memset(m, 0, sizeof(*m));
    memset(s, 0, sizeof(*s));
    m->fw_rev = 3u << GENI_FW_PROTO_SHIFT;
    m->hw_param0 = (32u << 24) | (DEPTH << 16);
    m->wrap_ver = 0x300a0000u;
    m->present = 1;
    m->if_disable = 0;
    s->se.ctx = s->wrapper.ctx = m;
    s->se.rd = se_rd; s->se.wr = se_wr;
    s->wrapper.rd = wrap_rd; s->wrapper.wr = no_wr;
    s->poll_limit = 200;
    if (dma_mode) {
        m->if_disable = GENI_FIFO_IF_DISABLE;
        s->use_dma = 1;
        static uint8_t tx[512], rx[512];
        s->dma.tx_virt = tx; s->dma.rx_virt = rx;
        s->dma.tx_phys = (uint64_t)(uintptr_t)tx;
        s->dma.rx_phys = (uint64_t)(uintptr_t)rx;
        s->dma.size = sizeof(tx);
    }
}

static void test_identify_and_clock(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 0);
    CHECK(geni_i2c_read_info(&s) == 0);
    CHECK(s.info.protocol == 3);
    CHECK(s.info.qup_hw_version == 0x300a0000u);
    CHECK(s.info.tx_fifo_depth == DEPTH);
    CHECK(s.info.fifo_width_bits == 32);
    CHECK(s.info.fifo_disabled == 0);
    CHECK(geni_i2c_init(&s) == 0);
    CHECK((m.reg[SE_GENI_DMA_MODE_EN / 4] & GENI_DMA_MODE_EN) == 0);
    CHECK(m.reg[GENI_FORCE_DEFAULT_REG / 4] == GENI_FORCE_DEFAULT);
    CHECK(m.reg[GENI_OUTPUT_CTRL / 4] == GENI_DEFAULT_IO_OUTPUT_CTRL);
    CHECK(geni_i2c_set_clock(&s, 400000) == 0);
    CHECK(m.reg[SE_I2C_SCL_COUNTERS / 4] == ((5u << 20) | (11u << 10) | 22u));
    CHECK(m.reg[GENI_SER_M_CLK_CFG / 4] == ((2u << 4) | 1u));
    CHECK(geni_i2c_set_clock(&s, 12345) != 0);
    printf("geni-i2c identify/init/clock: ok\n");
}

static void test_write_and_read(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 0);
    CHECK(geni_i2c_init(&s) == 0);
    CHECK(geni_i2c_set_clock(&s, 1000000) == 0);
    const uint8_t payload[] = {0x00, 0x01, 0x02, 0x00, 0x11, 0x22, 0x33};
    CHECK(geni_i2c_write(&s, 0x5d, payload, sizeof(payload), 1) == 0);
    // The first four bytes are the register address, the rest the payload. STOP clears the slave's address latch, so
    // verify through the memory window.
    CHECK(m.mem[0x010200] == 0x11 && m.mem[0x010201] == 0x22 && m.mem[0x010202] == 0x33);
    CHECK(m.sl_addr_bytes == 0);
    // A Goodix-style read: 4-byte address, stop-less write, then a read that ends with STOP.
    m.mem[0x10308] = 0x5a; m.mem[0x10309] = 0xa5;
    uint8_t addr[4] = {0x00, 0x01, 0x03, 0x08};
    uint8_t out[4] = {0};
    CHECK(geni_i2c_read(&s, 0x5d, addr, 4, out, sizeof(out)) == 0);
    CHECK(out[0] == 0x5a && out[1] == 0xa5);
    CHECK(m.byte_count == sizeof(payload) + 4 + sizeof(out));
    printf("geni-i2c write/read + stretch/stop: ok\n");
}

static void test_payload_order(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 0);
    CHECK(geni_i2c_init(&s) == 0);
    CHECK(geni_i2c_set_clock(&s, 400000) == 0);
    uint8_t tx[40] = {0x00, 0x01, 0x02, 0x03};
    for (int i = 4; i < 40; i++) tx[i] = (uint8_t)(0x80 + i);
    CHECK(geni_i2c_write(&s, 0x5d, tx, sizeof(tx), 1) == 0);
    CHECK(m.seen_n == sizeof(tx));
    CHECK(memcmp(m.seen, tx, sizeof(tx)) == 0);   // bytes leave the FIFO in order across word/depth refills
    for (int i = 4; i < 40; i++) CHECK(m.mem[0x00010203u + (uint32_t)(i - 4)] == tx[i]);
    printf("geni-i2c fifo refill order: ok\n");
}

static void test_nack_and_timeout(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 0);
    CHECK(geni_i2c_init(&s) == 0);
    CHECK(geni_i2c_set_clock(&s, 400000) == 0);
    m.present = 0;
    uint8_t b = 1;
    CHECK(geni_i2c_write(&s, 0x5d, &b, 1, 1) == GENI_ERR_NACK);
    CHECK(m.cancels == 1);                        // the failed command is cancelled before returning
    m.present = 1; m.stuck = 1;
    CHECK(geni_i2c_write(&s, 0x5d, &b, 1, 1) == GENI_ERR_TIMEOUT);
    CHECK(m.cancels == 2);
    printf("geni-i2c nack/timeout recovery: ok\n");
}

static void test_dma_mode(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 1);
    CHECK(geni_i2c_read_info(&s) == 0);
    CHECK(s.info.fifo_disabled == 1);
    CHECK(geni_i2c_init(&s) == 0);
    CHECK(m.reg[SE_GENI_DMA_MODE_EN / 4] & GENI_DMA_MODE_EN);
    CHECK(geni_i2c_set_clock(&s, 1000000) == 0);
    uint8_t addr[4] = {0x00, 0x01, 0x03, 0x08};
    m.mem[0x10308] = 0xde; m.mem[0x10309] = 0xad; m.mem[0x1030a] = 0xbe; m.mem[0x1030b] = 0xef;
    uint8_t out[4] = {0};
    CHECK(geni_i2c_read(&s, 0x5d, addr, 4, out, sizeof(out)) == 0);
    CHECK(out[0] == 0xde && out[3] == 0xef);
    CHECK(s.last_rx_in == 4);
    printf("geni-i2c dma read: ok\n");
}

static void test_fifo_disabled_refusal(void) {
    struct sim m; struct geni_i2c s;
    init_sim(&m, &s, 0);
    CHECK(geni_i2c_read_info(&s) == 0);
    m.if_disable = GENI_FIFO_IF_DISABLE;
    CHECK(geni_i2c_read_info(&s) == 0);
    CHECK(geni_i2c_init(&s) == GENI_ERR_NOTSUP);
    s.allow_fifo_disabled = 1;
    CHECK(geni_i2c_init(&s) == 0);
    printf("geni-i2c fifo-disabled gating: ok\n");
}

int main(void) {
    test_identify_and_clock();
    test_write_and_read();
    test_payload_order();
    test_nack_and_timeout();
    test_dma_mode();
    test_fifo_disabled_refusal();
    printf("PASS: geni i2c host model\n");
    return 0;
}
