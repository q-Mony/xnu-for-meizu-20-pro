// SPDX-License-Identifier: BSD-2-Clause-Patent
// Goodix Berlin D (GT9916) protocol: 4-byte big-endian register addresses, the boot-option handshake, the IC-info
// record (two on-wire layouts) and the touch event block. See GoodixBerlin.h for the sources.
#include "GoodixBerlin.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int gb_checksum_u8_ok(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    uint32_t sum = 0;
    for (size_t i = 0; i + 2 < size; i++) sum += data[i];
    return (sum & 0xffffu) == le16(data + size - 2);
}

static void put_reg_addr(uint8_t out[GB_REG_ADDR_LEN], uint32_t reg) {
    out[0] = (uint8_t)(reg >> 24);
    out[1] = (uint8_t)(reg >> 16);
    out[2] = (uint8_t)(reg >> 8);
    out[3] = (uint8_t)reg;
}

int gb_read_reg(struct gb_dev *d, uint32_t reg, void *buf, size_t len) {
    uint8_t *out = buf;
    uint8_t addr[GB_REG_ADDR_LEN];
    while (len) {
        size_t chunk = len > GB_MAX_READ ? GB_MAX_READ : len;
        put_reg_addr(addr, reg);
        int r = geni_i2c_read(d->bus, d->addr, addr, GB_REG_ADDR_LEN, out, chunk);
        if (r) {
            d->bus_errors++;
            return r;
        }
        out += chunk;
        reg += (uint32_t)chunk;
        len -= chunk;
    }
    return GB_ERR_OK;
}

int gb_write_reg(struct gb_dev *d, uint32_t reg, const void *buf, size_t len) {
    const uint8_t *in = buf;
    uint8_t msg[GB_REG_ADDR_LEN + GB_MAX_WRITE];
    while (len) {
        size_t chunk = len > GB_MAX_WRITE ? GB_MAX_WRITE : len;
        put_reg_addr(msg, reg);
        memcpy(msg + GB_REG_ADDR_LEN, in, chunk);
        int r = geni_i2c_write(d->bus, d->addr, msg, GB_REG_ADDR_LEN + chunk, 1);
        if (r) {
            d->bus_errors++;
            return r;
        }
        in += chunk;
        reg += (uint32_t)chunk;
        len -= chunk;
    }
    return GB_ERR_OK;
}

int gb_dev_confirm(struct gb_dev *d) {
    uint8_t tx[GB_DEV_CONFIRM_LEN], rx[GB_DEV_CONFIRM_LEN];
    memset(tx, GB_DEV_CONFIRM_VAL, sizeof(tx));
    for (int attempt = 0; attempt < 3; attempt++) {
        int r = gb_write_reg(d, GB_REG_BOOTOPTION, tx, sizeof(tx));
        if (r) return r;
        r = gb_read_reg(d, GB_REG_BOOTOPTION, rx, sizeof(rx));
        if (r) return r;
        if (!memcmp(tx, rx, sizeof(tx))) return GB_ERR_OK;
        if (d->bus->se.delay_us) d->bus->se.delay_us(d->bus->se.ctx, 5000);
    }
    return GB_ERR_FORMAT;
}

int gb_read_version(struct gb_dev *d, uint8_t version[26]) {
    int r = gb_read_reg(d, GB_REG_FW_VERSION, version, 26);
    if (r) return r;
    if (!gb_checksum_u8_ok(version, 26)) return GB_ERR_CHECKSUM;
    return GB_ERR_OK;
}

static int info_plausible(const struct gb_info *info) {
    if (info->touch_data_addr < 0x1000 || info->touch_data_addr > 0x7ffff) return 0;
    if (info->head_len != GB_EVENT_HEAD_LEN) return 0;
    if (info->point_len != GB_POINT_LEN) return 0;
    if (info->screen_max_x < 100 || info->screen_max_x > 8192) return 0;
    if (info->screen_max_y < 100 || info->screen_max_y > 8192) return 0;
    return 1;
}

// Fixed record: u16 length, 16-byte version, 10-byte feature, then a variable-length parameter block whose last
// fields are frequency tables, then the address block. Every copy in the vendor driver is sequential, so the wire
// layout is tightly packed.
static int parse_v1(struct gb_info *info, const uint8_t *b, size_t len) {
    size_t o = 2;
    if (len < o + 16 + 10 + 5) return GB_ERR_FORMAT;
    info->info_version = b[3];
    info->ic_die_id = b[4];
    info->ic_version_id = b[5];
    o += 16 + 10;
    info->drv_num = b[o];
    info->sen_num = b[o + 1];
    unsigned rates = b[o + 4];
    o += 5;
    if (rates > 8 || len < o + rates * 2u) return GB_ERR_FORMAT;
    o += rates * 2u;
    for (int i = 0; i < 4; i++) {
        if (len < o + 1) return GB_ERR_FORMAT;
        unsigned n = b[o++];
        if (n > 8 || len < o + n * 2u) return GB_ERR_FORMAT;
        o += n * 2u;
    }
    if (len < o + 122) return GB_ERR_FORMAT;
    const uint8_t *m = b + o;
    info->cmd_addr = le32(m + 0);
    info->touch_data_addr = le32(m + 44);
    info->head_len = le16(m + 48);
    info->point_len = le16(m + 50);
    info->screen_max_x = le16(m + 52);
    info->screen_max_y = le16(m + 54);
    info->esd_addr = le32(m + 110);
    if (info->head_len == 0) info->head_len = GB_EVENT_HEAD_LEN;
    if (info->point_len == 0) info->point_len = GB_POINT_LEN;
    info->source = GB_INFO_V1;
    return GB_ERR_OK;
}

struct v2_fields {
    uint32_t touch_data_addr;
    uint16_t head_len, point_len, max_x, max_y;
};

static void v2_read(struct v2_fields *f, const uint8_t *p, int packed) {
    f->touch_data_addr = le32(p + (packed ? 7 : 8));
    f->head_len = le16(p + (packed ? 11 : 12));
    f->point_len = le16(p + (packed ? 13 : 14));
    f->max_x = le16(p + (packed ? 15 : 16));
    f->max_y = le16(p + (packed ? 17 : 18));
}

// Package-tagged record: the vendor copies each package's bytes straight over the matching C struct, and that struct
// has one alignment hole after frame_data_support, so the protocol package can be read with or without that hole.
// Accept a candidate only if it yields a usable event window, and prefer the padded reading the vendor's memcpy
// implies.
static int parse_v2(struct gb_info *info, const uint8_t *b, size_t len) {
    if (len < 12 + 4) return GB_ERR_FORMAT;
    info->info_version = b[3];
    size_t o = 12;  // 2-byte length, then the 10-byte compatible head
    int have_protocol = 0;
    struct v2_fields padded = {0}, packed = {0};
    while (o + 4 <= len - 2) {
        unsigned pkg_len = b[o];
        uint32_t tag = (uint32_t)b[o + 1] | ((uint32_t)b[o + 2] << 8) | ((uint32_t)b[o + 3] << 16);
        if (pkg_len < 4 || o + pkg_len > len) break;
        const uint8_t *p = b + o + 4;
        if (tag == 20 && pkg_len >= 8) {
            info->drv_num = p[0];
            info->sen_num = p[1];
            info->ic_die_id = p[2];
            info->ic_version_id = p[3];
        } else if (tag == 160 && pkg_len >= 24) {
            v2_read(&padded, p, 0);
            v2_read(&packed, p, 1);
            have_protocol = 1;
        }
        o += pkg_len;
    }
    if (!have_protocol) return GB_ERR_FORMAT;
    info->touch_data_addr = padded.touch_data_addr;
    info->head_len = padded.head_len ? padded.head_len : GB_EVENT_HEAD_LEN;
    info->point_len = padded.point_len ? padded.point_len : GB_POINT_LEN;
    info->screen_max_x = padded.max_x;
    info->screen_max_y = padded.max_y;
    if (!info_plausible(info)) {
        info->touch_data_addr = packed.touch_data_addr;
        info->head_len = packed.head_len ? packed.head_len : GB_EVENT_HEAD_LEN;
        info->point_len = packed.point_len ? packed.point_len : GB_POINT_LEN;
        info->screen_max_x = packed.max_x;
        info->screen_max_y = packed.max_y;
        if (!info_plausible(info)) return GB_ERR_FORMAT;
    }
    info->source = GB_INFO_V2;
    return GB_ERR_OK;
}

int gb_parse_ic_info(struct gb_info *info, const uint8_t *blob, size_t len) {
    memset(info, 0, sizeof(*info));
    if (len < 6) return GB_ERR_FORMAT;
    uint16_t length = le16(blob);
    if (length < 6 || length > len) return GB_ERR_FORMAT;
    if (!gb_checksum_u8_ok(blob, length)) return GB_ERR_CHECKSUM;
    int r = blob[3] == 0x02 ? parse_v2(info, blob, length) : parse_v1(info, blob, length);
    info->blob_len = length;
    if (r) return r;
    if (!info_plausible(info)) return GB_ERR_FORMAT;
    return GB_ERR_OK;
}

int gb_read_ic_info(struct gb_dev *d) {
    uint8_t blob[GB_MAX_READ];
    int r = gb_read_reg(d, GB_REG_IC_INFO, blob, sizeof(blob));
    if (r) return r;
    r = gb_parse_ic_info(&d->info, blob, sizeof(blob));
    if (r) return r;
    return GB_ERR_OK;
}

int gb_parse_event(struct gb_event *ev, const uint8_t *buf, size_t len) {
    memset(ev, 0, sizeof(*ev));
    if (len < GB_EVENT_HEAD_LEN) return GB_ERR_FORMAT;
    if (buf[0] == 0) return 0;
    if (!gb_checksum_u8_ok(buf, GB_EVENT_HEAD_LEN)) return GB_ERR_CHECKSUM;
    ev->status = buf[0];
    ev->palm = (uint8_t)((buf[2] >> 4) & 1);
    ev->fp = buf[0] & GB_EVENT_FP;
    if (!(buf[0] & GB_EVENT_TOUCH)) return 0;

    unsigned count = buf[2] & 0x0f;
    ev->key = (uint8_t)((buf[3] >> 4) & 1);
    if (count > GB_MAX_TOUCH) return GB_ERR_FORMAT;
    unsigned points = count + (ev->key ? 1 : 0);
    size_t o = GB_EVENT_HEAD_LEN, sum_len = 0;
    for (unsigned i = 0; i < points; i++) {
        if (o + 1 > len) return GB_ERR_FORMAT;
        unsigned type = buf[o] & 0x0f;
        unsigned id = buf[o] >> 4;
        size_t need;
        switch (type) {
        case GB_POINT_FINGER:
            need = GB_POINT_LEN;
            break;
        case GB_POINT_STYLUS:
            need = GB_STYLUS_LEN;
            break;
        case GB_POINT_GLOVE:
            need = GB_POINT_LEN;
            break;
        case GB_POINT_KEY:
            need = GB_POINT_LEN;
            break;
        default:
            return GB_ERR_FORMAT;
        }
        if (o + need > len) return GB_ERR_FORMAT;
        if (type == GB_POINT_FINGER) {
            if (id >= GB_MAX_TOUCH) return GB_ERR_FORMAT;
            struct gb_point *p = &ev->points[ev->count];
            p->id = (uint8_t)id;
            p->x = le16(buf + o + 2);
            p->y = le16(buf + o + 4);
            p->w = le16(buf + o + 6);
            ev->count++;
        }
        sum_len += need;
        o += need;
    }
    if (o + 2 > len) return GB_ERR_FORMAT;
    if (!gb_checksum_u8_ok(buf + GB_EVENT_HEAD_LEN, sum_len + 2)) return GB_ERR_CHECKSUM;
    return 1;
}

int gb_clear_event(struct gb_dev *d) {
    uint8_t zero = 0;
    return gb_write_reg(d, d->info.touch_data_addr, &zero, 1);
}

int gb_poll_event(struct gb_dev *d, struct gb_event *ev) {
    uint8_t buf[GB_EVENT_MAX];
    memset(buf, 0, sizeof(buf));
    int r = gb_read_reg(d, d->info.touch_data_addr, buf, sizeof(buf));
    if (r) return r;
    if (buf[0] == 0) return 0;
    d->last_status = buf[0];
    r = gb_parse_event(ev, buf, sizeof(buf));
    // Acknowledge even on a checksum error: leaving the block set would make the next poll repeat it forever.
    int ack = gb_clear_event(d);
    if (r < 0) {
        d->checksum_errors++;
        return r;
    }
    if (r == 0) return 0;
    d->events++;
    return ack ? ack : 1;
}
