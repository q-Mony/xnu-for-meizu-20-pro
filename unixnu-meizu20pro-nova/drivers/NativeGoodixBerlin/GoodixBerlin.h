// SPDX-License-Identifier: BSD-2-Clause-Patent
// Goodix Berlin (GT9916, DT compatible "goodix,brl-d") register/event protocol over an I2C master.
// Everything here was taken from the vendor GPL driver (github.com/goodix/goodix_ts_berlin, goodix_brl_i2c.c,
// goodix_brl_hw.c, goodix_ts_utils.c) and the gt9916 porting guide; no code copied. The transport is the
// GENI I2C engine, but this module only needs a read/write pair, so it stays host-testable.
#ifndef GOODIX_BERLIN_H
#define GOODIX_BERLIN_H
#include <stddef.h>
#include <stdint.h>
#include "../NativeGeniI2c/GeniI2c.h"

// DT: goodix-berlin@5d on i2c@a90000.
#define GB_I2C_ADDR 0x5d
// An I2C message carries a 4-byte big-endian register address; the vendor caps a write message at 256 bytes and a
// read transfer at 256 bytes, so a payload is at most 252 bytes.
#define GB_REG_ADDR_LEN 4
#define GB_MAX_READ 256
#define GB_MAX_WRITE (GB_MAX_READ - GB_REG_ADDR_LEN)

// Berlin D (not Berlin A) command block addresses, goodix_brl_hw.c.
#define GB_REG_BOOTOPTION 0x10000u   // device confirm
#define GB_REG_FW_VERSION 0x10014u
#define GB_REG_IC_INFO 0x10070u
#define GB_DEV_CONFIRM_VAL 0xaa
#define GB_DEV_CONFIRM_LEN 8

// Event block returned at info.touch_data_addr.
#define GB_EVENT_MAX 170
#define GB_EVENT_HEAD_LEN 8
#define GB_POINT_LEN 8
#define GB_STYLUS_LEN 16
#define GB_MAX_TOUCH 10

#define GB_EVENT_TOUCH 0x80
#define GB_EVENT_REQUEST 0x40
#define GB_EVENT_GESTURE 0x20
#define GB_EVENT_FP 0x08
#define GB_REQUEST_CONFIG 0x01
#define GB_REQUEST_RESET 0x03

#define GB_POINT_FINGER 2
#define GB_POINT_STYLUS 3
#define GB_POINT_GLOVE 4
#define GB_POINT_KEY 5

#define GB_ERR_OK 0
#define GB_ERR_CHECKSUM (-74)
#define GB_ERR_FORMAT (-71)
#define GB_ERR_BUS (-5)

// Values used only if the IC-info block cannot be interpreted; the probe reports which source was used so a wrong
// fallback is visible in the boot log instead of silently moving the driver to a wrong register window.
#define GB_FALLBACK_TOUCH_ADDR 0x10308u
#define GB_FALLBACK_SCREEN_X 1440
#define GB_FALLBACK_SCREEN_Y 3200

enum gb_info_source {
    GB_INFO_NONE,
    GB_INFO_V1,        // fixed record: version, features, variable-length params, then the address block
    GB_INFO_V2,        // package-tagged record (info_version 0x02)
    GB_INFO_FALLBACK,
};

struct gb_info {
    uint32_t touch_data_addr;
    uint32_t cmd_addr;                 // v1 only: command block used for sleep/gesture writes
    uint32_t esd_addr;                 // v1 only
    uint16_t head_len;                 // bytes before the first point in an event block
    uint16_t point_len;                // bytes per finger point
    uint16_t screen_max_x;
    uint16_t screen_max_y;
    uint8_t info_version;              // raw afe_data[3]
    uint8_t ic_die_id;
    uint8_t ic_version_id;
    uint8_t drv_num;
    uint8_t sen_num;
    uint16_t blob_len;                 // IC-info length field
    enum gb_info_source source;
};

struct gb_point {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t w;
};

struct gb_event {
    uint8_t status;
    uint8_t count;                     // finger points only
    uint8_t palm;
    uint8_t key;                       // have_key flag from the head
    uint8_t fp;                        // fingerprint event flag
    struct gb_point points[GB_MAX_TOUCH];
};

struct gb_dev {
    struct geni_i2c *bus;
    unsigned addr;
    struct gb_info info;
    uint32_t events;
    uint32_t checksum_errors;
    uint32_t bus_errors;
    uint32_t last_status;
};

// Sum of every byte except the trailing little-endian 16-bit checksum equals that checksum.
int gb_checksum_u8_ok(const uint8_t *data, size_t size);

// Raw register access, chunked to the vendor's 252/256-byte message limits.
int gb_read_reg(struct gb_dev *d, uint32_t reg, void *buf, size_t len);
int gb_write_reg(struct gb_dev *d, uint32_t reg, const void *buf, size_t len);

// Write 8 x 0xaa to the boot option register and read them back; the first proof that the chip is powered and the
// bus/clock/pin path is right.
int gb_dev_confirm(struct gb_dev *d);
// Copy the 26-byte firmware version record (rom_pid, patch_pid, sensor_id, checksum).
int gb_read_version(struct gb_dev *d, uint8_t version[26]);
// Read the IC-info block and extract the event register window and panel geometry.
int gb_read_ic_info(struct gb_dev *d);
// Pure parser for a 256-byte IC-info snapshot (also used by host tests).
int gb_parse_ic_info(struct gb_info *info, const uint8_t *blob, size_t len);

// Parse an event snapshot; returns 1 for a touch event, 0 for any other status block, negative on a bad block.
int gb_parse_event(struct gb_event *ev, const uint8_t *buf, size_t len);
// Read an event block, parse it and acknowledge it to the chip. Returns 1 when a touch event was produced,
// 0 when there was nothing to report, negative on a bus/format error.
int gb_poll_event(struct gb_dev *d, struct gb_event *ev);
// Acknowledge the event block by writing zero to its first byte, as the vendor's after-event handler does.
int gb_clear_event(struct gb_dev *d);

static inline uint32_t gb_scale(uint16_t value, uint16_t max, uint32_t out_max) {
    if (!max) return 0;
    uint64_t scaled = ((uint64_t)value * out_max + max / 2) / max;
    return scaled > out_max ? out_max : (uint32_t)scaled;
}
#endif
