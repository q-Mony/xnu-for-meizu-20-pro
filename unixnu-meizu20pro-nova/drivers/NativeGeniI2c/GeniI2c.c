// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "GeniI2c.h"

static uint32_t rd(struct geni_i2c *s, uint32_t reg) { return s->se.rd(s->se.ctx, reg); }
static void wr(struct geni_i2c *s, uint32_t reg, uint32_t v) { s->se.wr(s->se.ctx, reg, v); }
static void rmw_set(struct geni_i2c *s, uint32_t reg, uint32_t bits) { wr(s, reg, rd(s, reg) | bits); }
static void delay_us(struct geni_i2c *s, unsigned us) { if (s->se.delay_us) s->se.delay_us(s->se.ctx, us); }

static void irq_clear(struct geni_i2c *s) {
    wr(s, SE_GSI_EVENT_EN, 0);
    wr(s, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
    wr(s, SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
    wr(s, SE_DMA_TX_IRQ_CLR, 0xffffffffu);
    wr(s, SE_DMA_RX_IRQ_CLR, 0xffffffffu);
    wr(s, SE_IRQ_EN, 0xffffffffu);
}

int geni_i2c_read_info(struct geni_i2c *s) {
    if (!s || !s->se.rd || !s->wrapper.rd) return GENI_ERR_PARAM;
    uint32_t fw = rd(s, GENI_FW_REVISION_RO);
    s->info.fw_revision = fw;
    s->info.protocol = (fw >> GENI_FW_PROTO_SHIFT) & 0xff;
    s->info.if_disable = rd(s, GENI_IF_DISABLE_RO);
    s->info.fifo_disabled = (s->info.if_disable & GENI_FIFO_IF_DISABLE) != 0;
    s->info.geni_status = rd(s, SE_GENI_STATUS);
    s->info.hw_param0 = rd(s, SE_HW_PARAM_0);
    s->info.qup_hw_version = s->wrapper.rd(s->wrapper.ctx, QUPV3_HW_VER_REG);
    uint32_t major = s->info.qup_hw_version >> 28, minor = (s->info.qup_hw_version >> 16) & 0xfff;
    uint32_t mask = ((major == 3 && minor >= 10) || major > 3) ? 0xff0000u : 0x3f0000u;
    s->info.tx_fifo_depth = (s->info.hw_param0 & mask) >> 16;
    s->info.fifo_width_bits = (s->info.hw_param0 >> 24) & 0x3f;
    return 0;
}

// 8-bit words, four per FIFO word, MSB first inside each byte: what the protocol drivers program for I2C.
static void config_packing(struct geni_i2c *s, unsigned bpw, unsigned pack_words, int msb_first) {
    uint32_t cfg[GENI_PACK_VECTORS] = {0};
    int idx_start = msb_first ? (int)bpw - 1 : 0, idx = idx_start;
    int idx_delta = msb_first ? -8 : 8;
    unsigned ceil_bpw = (bpw + 7) & ~7u;
    unsigned iter = ceil_bpw * pack_words / 8;
    int temp_bpw = (int)bpw;
    if (iter == 0 || iter > GENI_PACK_VECTORS) return;
    for (unsigned i = 0; i < iter; i++) {
        int len = (temp_bpw < 8 ? temp_bpw : 8) - 1;
        cfg[i] = ((uint32_t)idx << GENI_PACK_START_SHIFT) | ((uint32_t)(msb_first != 0) << GENI_PACK_DIR_SHIFT) |
                 ((uint32_t)len << GENI_PACK_LEN_SHIFT);
        if (temp_bpw <= 8) {
            idx = (int)(i + 1) * 8 + idx_start;
            temp_bpw = (int)bpw;
        } else {
            idx += idx_delta;
            temp_bpw -= 8;
        }
    }
    cfg[iter - 1] |= GENI_PACK_STOP;
    uint32_t cfg0 = cfg[0] | (cfg[1] << GENI_PACK_VECTOR_SHIFT);
    uint32_t cfg1 = cfg[2] | (cfg[3] << GENI_PACK_VECTOR_SHIFT);
    wr(s, SE_GENI_TX_PACKING_CFG0, cfg0);
    wr(s, SE_GENI_TX_PACKING_CFG1, cfg1);
    wr(s, SE_GENI_RX_PACKING_CFG0, cfg0);
    wr(s, SE_GENI_RX_PACKING_CFG1, cfg1);
    if (pack_words || bpw == 32) wr(s, SE_GENI_BYTE_GRAN, bpw / 16);
}

int geni_i2c_init(struct geni_i2c *s) {
    if (!s) return GENI_ERR_PARAM;
    int r = geni_i2c_read_info(s);
    if (r) return r;
    if (s->info.protocol != 3) return GENI_ERR_PROTO;              // GENI_SE_I2C
    if (s->use_dma && (!s->dma.tx_virt || !s->dma.rx_virt || !s->dma.tx_phys || !s->dma.rx_phys || !s->dma.size))
        return GENI_ERR_PARAM;
    if (s->info.fifo_disabled && !s->allow_fifo_disabled && !s->use_dma) return GENI_ERR_NOTSUP;
    if (s->info.tx_fifo_depth < 2 || s->info.fifo_width_bits != 32) return GENI_ERR_NODEV;
    s->tx_wm = s->info.tx_fifo_depth - 1;
    // geni_se_init
    irq_clear(s);
    rmw_set(s, SE_GENI_CGC_CTRL, GENI_DEFAULT_CGC_EN);
    rmw_set(s, SE_DMA_GENERAL_CFG, GENI_AHB_SEC_SLV_CGC_ON | GENI_DMA_AHB_SLV_CGC_ON | GENI_DMA_TX_CGC_ON | GENI_DMA_RX_CGC_ON);
    wr(s, GENI_OUTPUT_CTRL, GENI_DEFAULT_IO_OUTPUT_CTRL);
    wr(s, GENI_FORCE_DEFAULT_REG, GENI_FORCE_DEFAULT);
    rmw_set(s, SE_IRQ_EN, GENI_IRQ_M | GENI_IRQ_S | GENI_IRQ_DMA_TX | GENI_IRQ_DMA_RX);
    wr(s, SE_GENI_DMA_MODE_EN, rd(s, SE_GENI_DMA_MODE_EN) & ~GENI_DMA_MODE_EN);
    wr(s, SE_GSI_EVENT_EN, 0);
    wr(s, SE_GENI_RX_WATERMARK_REG, s->tx_wm);
    wr(s, SE_GENI_RX_RFR_WATERMARK_REG, s->info.tx_fifo_depth);
    rmw_set(s, SE_GENI_M_IRQ_EN, GENI_M_COMMON_IRQ_EN);
    rmw_set(s, SE_GENI_S_IRQ_EN, GENI_S_COMMON_IRQ_EN);
    config_packing(s, 8, 4, 1);
    // geni_se_select_mode
    irq_clear(s);
    if (s->use_dma) {
        wr(s, SE_GENI_M_IRQ_EN, rd(s, SE_GENI_M_IRQ_EN) &
           ~(GENI_M_CMD_DONE | GENI_M_TX_FIFO_WATERMARK | GENI_M_RX_FIFO_WATERMARK | GENI_M_RX_FIFO_LAST));
        wr(s, SE_GENI_DMA_MODE_EN, rd(s, SE_GENI_DMA_MODE_EN) | GENI_DMA_MODE_EN);
    } else {
        rmw_set(s, SE_GENI_M_IRQ_EN,
                GENI_M_CMD_DONE | GENI_M_TX_FIFO_WATERMARK | GENI_M_RX_FIFO_WATERMARK | GENI_M_RX_FIFO_LAST);
        wr(s, SE_GENI_DMA_MODE_EN, rd(s, SE_GENI_DMA_MODE_EN) & ~GENI_DMA_MODE_EN);
    }
    if (!s->poll_limit) s->poll_limit = 1000000;
    return 0;
}

struct i2c_clk {
    unsigned hz, div, high, low, cycle;
};

// 19.2 MHz source table of i2c-qcom-geni.c (clk_div, t_high_cnt, t_low_cnt, t_cycle_cnt).
static const struct i2c_clk clk_19p2mhz[] = {
    {100000, 7, 10, 12, 26},
    {400000, 2, 5, 11, 22},
    {1000000, 1, 2, 8, 18},
    {0, 0, 0, 0, 0},
};

int geni_i2c_set_clock(struct geni_i2c *s, unsigned hz) {
    if (!hz || s->clk_sel > 7) return GENI_ERR_PARAM;
    const struct i2c_clk *c = clk_19p2mhz;
    while (c->hz && c->hz != hz) c++;
    if (!c->hz) return GENI_ERR_PARAM;
    wr(s, SE_GENI_CLK_SEL, s->clk_sel);
    wr(s, GENI_SER_M_CLK_CFG, ((uint32_t)c->div << GENI_CLK_DIV_SHIFT) | GENI_SER_CLK_EN);
    uint32_t val = (c->high << GENI_HIGH_COUNTER_SHIFT) | (c->low << GENI_LOW_COUNTER_SHIFT) |
                   (c->cycle & GENI_CYCLE_COUNTER_MASK);
    wr(s, SE_I2C_SCL_COUNTERS, val);
    s->speed_hz = hz;
    return 0;
}

static int wait_bit(struct geni_i2c *s, uint32_t bit) {
    for (unsigned n = 0; n < s->poll_limit; n++) {
        uint32_t st = rd(s, SE_GENI_M_IRQ_STATUS);
        if (st & bit) {
            wr(s, SE_GENI_M_IRQ_CLEAR, st);
            return 0;
        }
        delay_us(s, 1);
    }
    return GENI_ERR_TIMEOUT;
}

static void recover(struct geni_i2c *s) {
    wr(s, SE_GENI_TX_WATERMARK_REG, 0);
    wr(s, SE_GENI_M_CMD_CTRL_REG, GENI_M_CMD_CANCEL);
    if (wait_bit(s, GENI_M_CMD_CANCEL_DONE | GENI_M_CMD_DONE) == 0) {
        if (s->use_dma) {
            wr(s, SE_DMA_TX_FSM_RST, 1);
            wr(s, SE_DMA_RX_FSM_RST, 1);
        }
        return;
    }
    wr(s, SE_GENI_M_CMD_CTRL_REG, GENI_M_CMD_ABORT);
    (void)wait_bit(s, GENI_M_CMD_ABORT_DONE | GENI_M_CMD_DONE);
    if (s->use_dma) {
        wr(s, SE_DMA_TX_FSM_RST, 1);
        wr(s, SE_DMA_RX_FSM_RST, 1);
    }
}

static unsigned bytes_per_word(const struct geni_i2c *s) { return s->info.fifo_width_bits / 8; }

struct xfer {
    const uint8_t *tx;
    uint8_t *rx;
    size_t tx_len, tx_rem, rx_len, rx_rem;
};

static void fifo_push(struct geni_i2c *s, struct xfer *x) {
    unsigned bpw = bytes_per_word(s);
    size_t max = (size_t)(s->info.tx_fifo_depth - s->tx_wm) * bpw;
    if (x->tx_rem < max) max = x->tx_rem;
    const uint8_t *p = x->tx + (x->tx_len - x->tx_rem);
    size_t i = 0;
    while (i < max) {
        uint32_t word = 0;
        size_t n = max - i < bpw ? max - i : bpw;
        for (size_t j = 0; j < n; j++) word |= (uint32_t)p[i++] << (8 * j);
        wr(s, SE_GENI_TX_FIFOn, word);
    }
    x->tx_rem -= max;
}

static void fifo_drain(struct geni_i2c *s, struct xfer *x) {
    unsigned bpw = bytes_per_word(s);
    uint32_t st = rd(s, SE_GENI_RX_FIFO_STATUS);
    size_t bytes = (size_t)(st & GENI_RX_WC_MASK) * bpw;
    if (st & GENI_RX_LAST) {
        unsigned valid = (st >> GENI_RX_LAST_VALID_SHIFT) & GENI_RX_LAST_VALID_MASK;
        if (valid && valid < bpw) bytes -= bpw - valid;
    }
    if (bytes > x->rx_rem) bytes = x->rx_rem;
    uint8_t *p = x->rx + (x->rx_len - x->rx_rem);
    size_t i = 0;
    while (i < bytes) {
        uint32_t word = rd(s, SE_GENI_RX_FIFOn);
        size_t n = bytes - i < bpw ? bytes - i : bpw;
        for (size_t j = 0; j < n; j++) p[i++] = (uint8_t)(word >> (8 * j));
    }
    x->rx_rem -= bytes;
}

static int transfer(struct geni_i2c *s, unsigned op, unsigned addr7, const uint8_t *tx, size_t txlen,
                    uint8_t *rx, size_t rxlen, int stop) {
    if (!s || (op != GENI_I2C_WRITE && op != GENI_I2C_READ) || addr7 > 0x7f) return GENI_ERR_PARAM;
    if ((!tx || !txlen) && (!rx || !rxlen)) return GENI_ERR_PARAM;
    if (txlen > 0xffffffu || rxlen > 0xffffffu) return GENI_ERR_PARAM;
    if (!s->speed_hz) return GENI_ERR_PARAM;
    if (s->use_dma && (txlen > s->dma.size || rxlen > s->dma.size)) return GENI_ERR_PARAM;

    if (tx && txlen) wr(s, SE_I2C_TX_TRANS_LEN, (uint32_t)txlen);
    if (rx && rxlen) wr(s, SE_I2C_RX_TRANS_LEN, (uint32_t)rxlen);
    wr(s, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
    wr(s, SE_DMA_TX_IRQ_CLR, 0xffffffffu);
    wr(s, SE_DMA_RX_IRQ_CLR, 0xffffffffu);
    uint32_t params = ((uint32_t)addr7 << GENI_I2C_SLV_ADDR_SHIFT) & GENI_I2C_SLV_ADDR_MASK;
    if (!stop) params |= GENI_I2C_STOP_STRETCH;
    wr(s, SE_GENI_M_CMD0, ((uint32_t)op << GENI_M_OPCODE_SHIFT) | params);

    if (s->use_dma) {
        if (rx && rxlen) {
            volatile uint8_t *rv0 = s->dma.rx_virt;
            for (size_t i = 0; i < rxlen; i++) rv0[i] = 0x5a;
            if (s->dma.clean_inval) s->dma.clean_inval(s->dma.ctx, s->dma.rx_virt, rxlen);
            wr(s, SE_DMA_RX_IRQ_EN_SET, GENI_DMA_DONE | GENI_DMA_EOT | GENI_DMA_AHB_ERR);
            wr(s, SE_DMA_RX_PTR_L, (uint32_t)s->dma.rx_phys);
            wr(s, SE_DMA_RX_PTR_H, (uint32_t)(s->dma.rx_phys >> 32));
            wr(s, SE_DMA_RX_ATTR, 0);
            wr(s, SE_DMA_RX_LEN, (uint32_t)rxlen);
        }
        if (tx && txlen) {
            volatile uint8_t *tv = s->dma.tx_virt;
            for (size_t i = 0; i < txlen; i++) tv[i] = tx[i];
            if (s->dma.clean) s->dma.clean(s->dma.ctx, s->dma.tx_virt, txlen);
            wr(s, SE_DMA_TX_IRQ_EN_SET, GENI_DMA_DONE | GENI_DMA_EOT | GENI_DMA_AHB_ERR);
            wr(s, SE_DMA_TX_PTR_L, (uint32_t)s->dma.tx_phys);
            wr(s, SE_DMA_TX_PTR_H, (uint32_t)(s->dma.tx_phys >> 32));
            wr(s, SE_DMA_TX_ATTR, GENI_DMA_TX_ATTR_EOT_BUF);
            wr(s, SE_DMA_TX_LEN, (uint32_t)txlen);
        }
    } else {
        struct xfer x = {tx, rx, txlen, tx ? txlen : 0, rxlen, rx ? rxlen : 0};
        if (tx && txlen) {
            fifo_push(s, &x);
            wr(s, SE_GENI_TX_WATERMARK_REG, 1);
        }
        int status = GENI_ERR_TIMEOUT;
        int nack = 0;
        for (unsigned n = 0; n < s->poll_limit; n++) {
            uint32_t ms = rd(s, SE_GENI_M_IRQ_STATUS);
            if (ms) wr(s, SE_GENI_M_IRQ_CLEAR, ms);
            if (ms & GENI_M_GP_IRQ_1) nack = 1;
            if (ms & GENI_M_ERRORS) { status = GENI_ERR_IO; break; }
            if (tx && (ms & GENI_M_TX_FIFO_WATERMARK) && x.tx_rem) {
                fifo_push(s, &x);
                if (!x.tx_rem) wr(s, SE_GENI_TX_WATERMARK_REG, 0);
            }
            if (rx && (ms & (GENI_M_RX_FIFO_WATERMARK | GENI_M_RX_FIFO_LAST))) fifo_drain(s, &x);
            if (ms & GENI_M_CMD_DONE) {
                if (rx && x.rx_rem) fifo_drain(s, &x);
                status = (x.tx_rem || x.rx_rem) ? GENI_ERR_IO : 0;
                if (!status && nack) status = GENI_ERR_NACK;   // a NACK ends the command with DONE set
                if (x.tx_rem) wr(s, SE_GENI_TX_WATERMARK_REG, 0);
                break;
            }
            delay_us(s, 1);
        }
        if (status) {
            s->snap_irq = rd(s, SE_GENI_M_IRQ_STATUS);
            s->snap_status = rd(s, SE_GENI_STATUS);
            recover(s);
            return nack ? GENI_ERR_NACK : status;
        }
        return 0;
    }

    int tx_done = !(tx && txlen), rx_done = !(rx && rxlen), status = GENI_ERR_TIMEOUT, nack = 0;
    for (unsigned n = 0; n < s->poll_limit; n++) {
        uint32_t ts = (tx && txlen) ? rd(s, SE_DMA_TX_IRQ_STAT) : 0;
        uint32_t rs = (rx && rxlen) ? rd(s, SE_DMA_RX_IRQ_STAT) : 0;
        uint32_t ms = rd(s, SE_GENI_M_IRQ_STATUS);
        if (ts) wr(s, SE_DMA_TX_IRQ_CLR, ts);
        if (rs) wr(s, SE_DMA_RX_IRQ_CLR, rs);
        if (ms) wr(s, SE_GENI_M_IRQ_CLEAR, ms);
        if (ms & GENI_M_GP_IRQ_1) nack = 1;
        if ((ts & GENI_DMA_AHB_ERR) || (rs & GENI_DMA_AHB_ERR) || (ms & GENI_M_ERRORS)) { status = GENI_ERR_IO; break; }
        if (ts & GENI_DMA_DONE) tx_done = 1;
        if (rs & GENI_DMA_DONE) rx_done = 1;
        if (tx_done && rx_done) { status = nack ? GENI_ERR_NACK : 0; break; }
        delay_us(s, 1);
    }
    s->last_tx_in = (tx && txlen) ? rd(s, SE_DMA_TX_LEN_IN) : 0;
    s->last_rx_in = (rx && rxlen) ? rd(s, SE_DMA_RX_LEN_IN) : 0;
    if (!status && rx && rxlen) {
        if (s->dma.inval) s->dma.inval(s->dma.ctx, s->dma.rx_virt, rxlen);
        volatile uint8_t *rv = s->dma.rx_virt;
        for (size_t i = 0; i < rxlen; i++) rx[i] = rv[i];
        return 0;
    }
    if (!status) return 0;
    s->snap_dma_tx = rd(s, SE_DMA_TX_IRQ_STAT);
    s->snap_dma_rx = rd(s, SE_DMA_RX_IRQ_STAT);
    s->snap_dma_txin = rd(s, SE_DMA_TX_LEN_IN);
    s->snap_dma_rxin = rd(s, SE_DMA_RX_LEN_IN);
    s->snap_irq = rd(s, SE_GENI_M_IRQ_STATUS);
    s->snap_status = rd(s, SE_GENI_STATUS);
    recover(s);
    return nack ? GENI_ERR_NACK : status;
}

int geni_i2c_write(struct geni_i2c *s, unsigned addr7, const uint8_t *buf, size_t len, int stop) {
    if (s->soft_write) return s->soft_write(s, addr7, buf, len, stop);
    return transfer(s, GENI_I2C_WRITE, addr7, buf, len, NULL, 0, stop);
}

int geni_i2c_read(struct geni_i2c *s, unsigned addr7, const uint8_t *tx, size_t txlen, uint8_t *rx, size_t rxlen) {
    if (s->soft_read) return s->soft_read(s, addr7, tx, txlen, rx, rxlen);
    int r = transfer(s, GENI_I2C_WRITE, addr7, tx, txlen, NULL, 0, 0);
    if (r) return r;
    return transfer(s, GENI_I2C_READ, addr7, NULL, 0, rx, rxlen, 1);
}
