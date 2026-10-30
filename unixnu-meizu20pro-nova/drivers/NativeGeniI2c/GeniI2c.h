// SPDX-License-Identifier: BSD-2-Clause-Patent
// Qualcomm GENI serial-engine I2C master. Register layout, command encoding and the two transfer modes
// (FIFO and the SE's own DMA) follow the Linux i2c-qcom-geni.c / qcom-geni-se.c study; no code copied.
// No board addresses, clocks or pins here: the caller owns MMIO, GCC votes, TLMM and SMMU mapping.
#ifndef GENI_I2C_H
#define GENI_I2C_H
#include <stdint.h>
#include <stddef.h>

// Serial-engine registers (offsets from the SE base).
#define GENI_FORCE_DEFAULT_REG 0x20
#define GENI_OUTPUT_CTRL 0x24
#define SE_GENI_CGC_CTRL 0x28
#define SE_GENI_STATUS 0x40
#define GENI_SER_M_CLK_CFG 0x48
#define GENI_IF_DISABLE_RO 0x64
#define GENI_FW_REVISION_RO 0x68
#define SE_GENI_CLK_SEL 0x7c
#define SE_GENI_BYTE_GRAN 0x254
#define SE_GENI_DMA_MODE_EN 0x258
#define SE_GENI_TX_PACKING_CFG0 0x260
#define SE_GENI_TX_PACKING_CFG1 0x264
#define SE_I2C_TX_TRANS_LEN 0x26c
#define SE_I2C_RX_TRANS_LEN 0x270
#define SE_I2C_SCL_COUNTERS 0x278
#define SE_GENI_RX_PACKING_CFG0 0x284
#define SE_GENI_RX_PACKING_CFG1 0x288
#define SE_GENI_M_CMD0 0x600
#define SE_GENI_M_CMD_CTRL_REG 0x604
#define SE_GENI_M_IRQ_STATUS 0x610
#define SE_GENI_M_IRQ_EN 0x614
#define SE_GENI_M_IRQ_CLEAR 0x618
#define SE_GENI_S_IRQ_EN 0x644
#define SE_GENI_S_IRQ_CLEAR 0x648
#define SE_DMA_TX_IRQ_CLR 0xc44
#define SE_DMA_RX_IRQ_CLR 0xd44
#define SE_GENI_TX_FIFOn 0x700
#define SE_GENI_RX_FIFOn 0x780
#define SE_GENI_TX_FIFO_STATUS 0x800
#define SE_GENI_RX_FIFO_STATUS 0x804
#define SE_GENI_TX_WATERMARK_REG 0x80c
#define SE_GENI_RX_WATERMARK_REG 0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_GSI_EVENT_EN 0xe18
#define SE_IRQ_EN 0xe1c
#define SE_HW_PARAM_0 0xe24
#define SE_DMA_GENERAL_CFG 0xe30
// Serial-engine DMA registers.
#define SE_DMA_TX_PTR_L 0xc30
#define SE_DMA_TX_PTR_H 0xc34
#define SE_DMA_TX_ATTR 0xc38
#define SE_DMA_TX_LEN 0xc3c
#define SE_DMA_TX_IRQ_STAT 0xc40
#define SE_DMA_TX_IRQ_EN_SET 0xc4c
#define SE_DMA_TX_LEN_IN 0xc54
#define SE_DMA_TX_FSM_RST 0xc58
#define SE_DMA_RX_PTR_L 0xd30
#define SE_DMA_RX_PTR_H 0xd34
#define SE_DMA_RX_ATTR 0xd38
#define SE_DMA_RX_LEN 0xd3c
#define SE_DMA_RX_IRQ_STAT 0xd40
#define SE_DMA_RX_IRQ_EN_SET 0xd4c
#define SE_DMA_RX_LEN_IN 0xd54
#define SE_DMA_RX_FSM_RST 0xd58
// Wrapper register (offset from the QUP wrapper base).
#define QUPV3_HW_VER_REG 0x4

// M_CMD opcodes for I2C.
#define GENI_I2C_WRITE 0x1
#define GENI_I2C_READ 0x2
#define GENI_I2C_WRITE_READ 0x3
#define GENI_I2C_ADDR_ONLY 0x4
// M_CMD params for I2C.
#define GENI_I2C_STOP_STRETCH (1u << 2)      // no STOP at the end: the next command continues the transfer
#define GENI_I2C_SLV_ADDR_SHIFT 9
#define GENI_I2C_SLV_ADDR_MASK 0xfe00u

// Register field values.
#define GENI_FORCE_DEFAULT (1u << 0)
#define GENI_SER_CLK_EN (1u << 0)
#define GENI_CLK_DIV_SHIFT 4
#define GENI_CLK_DIV_MAX 0xfffu
#define GENI_FIFO_IF_DISABLE (1u << 0)
#define GENI_FW_PROTO_SHIFT 8
#define GENI_DMA_MODE_EN (1u << 0)
#define GENI_DEFAULT_CGC_EN 0x7fu
#define GENI_DEFAULT_IO_OUTPUT_CTRL 0x7fu
#define GENI_M_OPCODE_SHIFT 27
#define GENI_M_PARAMS_MASK 0x7ffffffu
#define GENI_M_CMD_CANCEL (1u << 2)
#define GENI_M_CMD_ABORT (1u << 1)
#define GENI_M_CMD_DONE (1u << 0)
#define GENI_M_CMD_OVERRUN (1u << 1)
#define GENI_M_ILLEGAL_CMD (1u << 2)
#define GENI_M_CMD_FAILURE (1u << 3)
#define GENI_M_CMD_CANCEL_DONE (1u << 4)
#define GENI_M_CMD_ABORT_DONE (1u << 5)
#define GENI_M_GP_IRQ_0 (1u << 9)
#define GENI_M_GP_IRQ_1 (1u << 10)           // address/data NACK
#define GENI_M_GP_IRQ_3 (1u << 12)           // bus protocol error
#define GENI_M_GP_IRQ_4 (1u << 13)           // arbitration lost
#define GENI_M_IO_DATA_DEASSERT (1u << 22)
#define GENI_M_IO_DATA_ASSERT (1u << 23)
#define GENI_M_RX_FIFO_RD_ERR (1u << 24)
#define GENI_M_RX_FIFO_WR_ERR (1u << 25)
#define GENI_M_RX_FIFO_WATERMARK (1u << 26)
#define GENI_M_RX_FIFO_LAST (1u << 27)
#define GENI_M_TX_FIFO_RD_ERR (1u << 28)
#define GENI_M_TX_FIFO_WR_ERR (1u << 29)
#define GENI_M_TX_FIFO_WATERMARK (1u << 30)
#define GENI_M_COMMON_IRQ_EN (0x7eu | GENI_M_IO_DATA_DEASSERT | GENI_M_IO_DATA_ASSERT | GENI_M_RX_FIFO_RD_ERR | \
                              GENI_M_RX_FIFO_WR_ERR | GENI_M_TX_FIFO_RD_ERR | GENI_M_TX_FIFO_WR_ERR)
#define GENI_S_COMMON_IRQ_EN (0x3eu | 0x3e00u | (1u << 24) | (1u << 25))
// Bus errors that end a transfer without a further FIFO/DMA action.
#define GENI_M_ERRORS (GENI_M_CMD_OVERRUN | GENI_M_ILLEGAL_CMD | GENI_M_CMD_FAILURE | GENI_M_GP_IRQ_0 | \
                       GENI_M_GP_IRQ_3 | GENI_M_GP_IRQ_4 | GENI_M_RX_FIFO_RD_ERR | GENI_M_RX_FIFO_WR_ERR | \
                       GENI_M_TX_FIFO_RD_ERR | GENI_M_TX_FIFO_WR_ERR)
// SE_IRQ_EN
#define GENI_IRQ_DMA_RX (1u << 0)
#define GENI_IRQ_DMA_TX (1u << 1)
#define GENI_IRQ_M (1u << 2)
#define GENI_IRQ_S (1u << 3)
// SE_DMA_GENERAL_CFG
#define GENI_DMA_RX_CGC_ON (1u << 0)
#define GENI_DMA_TX_CGC_ON (1u << 1)
#define GENI_DMA_AHB_SLV_CGC_ON (1u << 2)
#define GENI_AHB_SEC_SLV_CGC_ON (1u << 3)
// SE_DMA_{TX,RX}_IRQ_STAT / _EN_SET fields
#define GENI_DMA_DONE (1u << 0)
#define GENI_DMA_EOT (1u << 1)
#define GENI_DMA_AHB_ERR (1u << 2)
#define GENI_DMA_RESET_DONE (1u << 3)
#define GENI_DMA_TX_ATTR_EOT_BUF (1u << 0)
// RX_FIFO_STATUS
#define GENI_RX_LAST (1u << 31)
#define GENI_RX_LAST_VALID_SHIFT 28
#define GENI_RX_LAST_VALID_MASK 7u
#define GENI_RX_WC_MASK 0x1ffffffu
// Packing vector fields
#define GENI_PACK_START_SHIFT 5
#define GENI_PACK_DIR_SHIFT 4
#define GENI_PACK_LEN_SHIFT 1
#define GENI_PACK_STOP (1u << 0)
#define GENI_PACK_VECTOR_SHIFT 10
#define GENI_PACK_VECTORS 4
// I2C SCL counter fields
#define GENI_HIGH_COUNTER_SHIFT 20
#define GENI_LOW_COUNTER_SHIFT 10
#define GENI_CYCLE_COUNTER_MASK 0x3ffu

#define GENI_ERR_PARAM (-22)
#define GENI_ERR_PROTO (-71)
#define GENI_ERR_NODEV (-19)
#define GENI_ERR_TIMEOUT (-110)
#define GENI_ERR_IO (-5)
#define GENI_ERR_NOTSUP (-95)
#define GENI_ERR_NACK (-6)

struct geni_io {
    void *ctx;
    uint32_t (*rd)(void *ctx, uint32_t reg);
    void (*wr)(void *ctx, uint32_t reg, uint32_t value);
    void (*delay_us)(void *ctx, unsigned us);
};

struct geni_i2c_info {
    uint32_t fw_revision, protocol, if_disable, hw_param0, geni_status;
    uint32_t qup_hw_version;
    unsigned tx_fifo_depth, fifo_width_bits;
    int fifo_disabled;
};

struct geni_i2c {
    struct geni_io se;                  // serial engine registers
    struct geni_io wrapper;             // QUP wrapper registers (hardware version only)
    struct geni_i2c_info info;
    unsigned clk_sel;                   // SE_GENI_CLK_SEL value: DFS level carrying the 19.2 MHz source
    unsigned speed_hz;
    unsigned poll_limit;                // completion poll iterations before cancel/abort
    unsigned tx_wm;
    int use_dma;                        // FIFO interface disabled: move payloads with the SE DMA master
    int allow_fifo_disabled;            // try FIFO even when FIFO_IF_DISABLE is set (diagnosis only)
    // Two caller-provided buffers the SE DMA master must be able to reach at tx_phys/rx_phys. The caller owns the
    // translation (SMMU) and the cache policy; the callbacks maintain the CPU caches around each transfer.
    struct geni_i2c_dma {
        void *ctx;
        void (*clean)(void *ctx, void *virt, size_t len);
        void (*clean_inval)(void *ctx, void *virt, size_t len);
        void (*inval)(void *ctx, void *virt, size_t len);
        uint8_t *tx_virt, *rx_virt;
        uint64_t tx_phys, rx_phys;
        size_t size;
    } dma;
    uint32_t last_tx_in, last_rx_in;    // SE_DMA_{TX,RX}_LEN_IN: bytes the engine moved
    uint32_t snap_irq, snap_status, snap_dma_tx, snap_dma_rx, snap_dma_txin, snap_dma_rxin;
    // Optional software transport: when set, the engine registers are not used
    // (the QUP reports FIFO_IF_DISABLE and has no usable legacy DMA).
    int (*soft_write)(struct geni_i2c *, unsigned addr7, const uint8_t *buf, size_t len, int stop);
    int (*soft_read)(struct geni_i2c *, unsigned addr7, const uint8_t *tx, size_t txlen,
                     uint8_t *rx, size_t rxlen);
    void *soft_ctx;
};

// Read-only identity of the engine. Only valid with the SE clocks running.
int geni_i2c_read_info(struct geni_i2c *s);
// Program the engine defaults and select FIFO or SE-DMA mode according to FIFO_IF_DISABLE.
int geni_i2c_init(struct geni_i2c *s);
// Program SE_GENI_CLK_SEL and the SCL counters for one of 100 kHz, 400 kHz or 1 MHz from a 19.2 MHz source.
int geni_i2c_set_clock(struct geni_i2c *s, unsigned hz);
// One master write of len bytes; stop = 1 issues STOP, stop = 0 holds SCL for a following read.
int geni_i2c_write(struct geni_i2c *s, unsigned addr7, const uint8_t *buf, size_t len, int stop);
// Write txlen bytes (the register address), then read rxlen bytes, with STOP only after the read.
int geni_i2c_read(struct geni_i2c *s, unsigned addr7, const uint8_t *tx, size_t txlen, uint8_t *rx, size_t rxlen);
#endif
