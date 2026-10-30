// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test: GoodixBerlin.c against synthetic IC-info records and event blocks, plus a stubbed register transport.
// The layouts are built byte by byte from the vendor driver's parsing code, so a mismatch here means the parser and
// the protocol disagree before anything touches hardware.
#include "../GoodixBerlin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) do { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); exit(1); } while (0)
#define CHECK(c) do { if (!(c)) FAIL("%s", #c); } while (0)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Trailing 16-bit checksum: sum of every other byte.
static void seal(uint8_t *buf, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 2 < len; i++) sum += buf[i];
    put16(buf + len - 2, (uint16_t)sum);
}

static void test_checksum(void) {
    // The last two bytes carry the little-endian sum of everything before them.
    uint8_t ok[5] = {0x01, 0x02, 0x03, 0x06, 0x00};
    CHECK(gb_checksum_u8_ok(ok, sizeof(ok)));
    ok[3] = 0x07;
    CHECK(!gb_checksum_u8_ok(ok, sizeof(ok)));
    CHECK(!gb_checksum_u8_ok(ok, 1));
    // Wider than one byte: the sum is truncated to 16 bits.
    uint8_t wide[6] = {0xff, 0xff, 0xff, 0x02, 0xff, 0x02};
    CHECK(gb_checksum_u8_ok(wide, sizeof(wide)));
    printf("goodix checksum: ok\n");
}

// Version (16) + feature (10) + params (5 + 4 count bytes) + address block (122) + checksum.
static size_t build_v1(uint8_t *b, uint32_t touch_addr, uint16_t max_x, uint16_t max_y) {
    memset(b, 0, 256);
    size_t o = 2;
    b[o + 0] = 0x11;             // info_customer_id
    b[o + 1] = 0x01;             // info_version_id
    b[o + 2] = 0x40;             // ic_die_id
    b[o + 3] = 0x01;             // ic_version_id: not 0x02, so the fixed record is expected
    o += 16;
    o += 10;                     // feature block
    b[o + 0] = 18;               // drv_num
    b[o + 1] = 40;               // sen_num
    b[o + 2] = 0;
    b[o + 3] = 0;
    b[o + 4] = 0;                // active_scan_rate_num
    o += 5;
    o += 4;                      // four empty frequency tables
    uint8_t *m = b + o;
    put32(m + 0, 0x10330);       // cmd_addr
    put32(m + 44, touch_addr);
    put16(m + 48, GB_EVENT_HEAD_LEN);
    put16(m + 50, GB_POINT_LEN);
    put16(m + 52, max_x);
    put16(m + 54, max_y);
    put32(m + 110, 0x10360);     // esd_addr
    size_t len = o + 122 + 2;
    put16(b, (uint16_t)len);
    seal(b, len);
    return len;
}

static void test_v1(void) {
    uint8_t b[256];
    size_t len = build_v1(b, 0x10308, 1440, 3200);
    struct gb_info info;
    CHECK(gb_parse_ic_info(&info, b, len) == GB_ERR_OK);
    CHECK(info.source == GB_INFO_V1);
    CHECK(info.touch_data_addr == 0x10308);
    CHECK(info.head_len == 8 && info.point_len == 8);
    CHECK(info.screen_max_x == 1440 && info.screen_max_y == 3200);
    CHECK(info.cmd_addr == 0x10330 && info.esd_addr == 0x10360);
    CHECK(info.drv_num == 18 && info.sen_num == 40);
    // A single wrong byte must be rejected, not silently used.
    b[60] ^= 0x40;
    CHECK(gb_parse_ic_info(&info, b, len) == GB_ERR_CHECKSUM);
    printf("goodix ic-info v1: ok\n");
}

static size_t build_v2(uint8_t *b, int packed, uint32_t touch_addr, uint16_t max_x, uint16_t max_y) {
    memset(b, 0, 256);
    size_t o = 2;
    b[o + 0] = 0x22;
    b[o + 1] = 0x01;
    put32(b + o + 2, 0);
    put16(b + o + 6, 0);
    o += 10;
    // BSP package (tag 20): driver/sensor counts and die identity.
    b[o + 0] = 8;
    b[o + 1] = 20; b[o + 2] = 0; b[o + 3] = 0;
    b[o + 4] = 18; b[o + 5] = 40; b[o + 6] = 0x40; b[o + 7] = 0x01;
    o += 8;
    // Protocol package (tag 160): the event window and the panel geometry.
    b[o + 0] = 24;
    b[o + 1] = 160; b[o + 2] = 0; b[o + 3] = 0;
    uint8_t *p = b + o + 4;
    if (packed) memset(p, 0xff, 20);
    put32(p + 0, 0x10000);       // frame_data_addr
    put16(p + 4, 8);             // frame_data_head_len
    p[6] = 0;                    // frame_data_support
    if (packed) {
        put32(p + 7, touch_addr);
        put16(p + 11, GB_EVENT_HEAD_LEN);
        put16(p + 13, GB_POINT_LEN);
        put16(p + 15, max_x);
        put16(p + 17, max_y);
    } else {
        put32(p + 8, touch_addr);   // the struct has one alignment byte after frame_data_support
        put16(p + 12, GB_EVENT_HEAD_LEN);
        put16(p + 14, GB_POINT_LEN);
        put16(p + 16, max_x);
        put16(p + 18, max_y);
    }
    o += 24;
    size_t len = o + 2;
    put16(b, (uint16_t)len);
    b[3] = 0x02;                 // info_version selects the package record
    seal(b, len);
    return len;
}

static void test_v2(void) {
    uint8_t b[256];
    struct gb_info info;
    size_t len = build_v2(b, 0, 0x10308, 1440, 3200);
    CHECK(gb_parse_ic_info(&info, b, len) == GB_ERR_OK);
    CHECK(info.source == GB_INFO_V2);
    CHECK(info.touch_data_addr == 0x10308);
    CHECK(info.head_len == 8 && info.point_len == 8);
    CHECK(info.screen_max_x == 1440 && info.screen_max_y == 3200);

    // The packed reading must be found when the aligned one produces nonsense.
    len = build_v2(b, 1, 0x10308, 1440, 3200);
    CHECK(gb_parse_ic_info(&info, b, len) == GB_ERR_OK);
    CHECK(info.source == GB_INFO_V2);
    CHECK(info.touch_data_addr == 0x10308);
    CHECK(info.screen_max_x == 1440 && info.screen_max_y == 3200);

    // A truncated record is refused rather than steering the driver to a bogus window.
    CHECK(gb_parse_ic_info(&info, b, 20) == GB_ERR_FORMAT);
    printf("goodix ic-info v2 (aligned and packed): ok\n");
}

static void test_geometry_sanity(void) {
    uint8_t b[256];
    struct gb_info info;
    size_t len = build_v1(b, 0x10308, 0, 3200);   // no width: implausible
    CHECK(gb_parse_ic_info(&info, b, len) != GB_ERR_OK);
    len = build_v1(b, 0x10308, 1440, 3200);
    CHECK(gb_parse_ic_info(&info, b, len) == GB_ERR_OK);
    CHECK(gb_scale(720, 1440, 30480) == 15240);
    CHECK(gb_scale(0, 1440, 30480) == 0);
    CHECK(gb_scale(1440, 1440, 30480) == 30480);
    CHECK(gb_scale(100, 0, 30480) == 0);
    printf("goodix geometry sanity/scaling: ok\n");
}

static size_t build_event(uint8_t *b, unsigned fingers, int key) {
    memset(b, 0, GB_EVENT_MAX);
    b[0] = GB_EVENT_TOUCH;
    b[2] = (uint8_t)(fingers & 0x0f);          // palm flag clear
    b[3] = key ? 0x10 : 0x00;
    size_t o = GB_EVENT_HEAD_LEN, sum = 0;
    for (unsigned i = 0; i < fingers; i++) {
        b[o + 0] = (uint8_t)(GB_POINT_FINGER | ((i + 3) << 4));
        put16(b + o + 2, (uint16_t)(100 + i * 10));
        put16(b + o + 4, (uint16_t)(200 + i * 20));
        put16(b + o + 6, 40);
        o += GB_POINT_LEN;
        sum += GB_POINT_LEN;
    }
    if (key) {
        b[o + 0] = GB_POINT_KEY;
        b[o + 2] = 0x01;                        // key 0 down
        o += GB_POINT_LEN;
        sum += GB_POINT_LEN;
    }
    uint32_t cs = 0;
    for (size_t i = 0; i < sum; i++) cs += b[GB_EVENT_HEAD_LEN + i];
    put16(b + o, (uint16_t)cs);
    o += 2;
    seal(b, GB_EVENT_HEAD_LEN);                 // head checksum covers bytes 0..5
    return o;
}

static void test_event_parse(void) {
    uint8_t b[GB_EVENT_MAX];
    struct gb_event ev;
    build_event(b, 2, 0);
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == 1);
    CHECK(ev.count == 2 && ev.key == 0 && ev.palm == 0);
    CHECK(ev.points[0].id == 3 && ev.points[0].x == 100 && ev.points[0].y == 200 && ev.points[0].w == 40);
    CHECK(ev.points[1].id == 4 && ev.points[1].x == 110 && ev.points[1].y == 220);

    build_event(b, 1, 1);
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == 1);
    CHECK(ev.count == 1 && ev.key == 1);

    // Palm flag sits in the high nibble of byte 2.
    build_event(b, 1, 0);
    b[2] |= 0x10;
    seal(b, GB_EVENT_HEAD_LEN);
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == 1);
    CHECK(ev.palm == 1);

    // No event: byte 0 clear.
    memset(b, 0, sizeof(b));
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == 0);

    // Corrupt point data is refused.
    build_event(b, 2, 0);
    b[GB_EVENT_HEAD_LEN + 3] ^= 0x10;
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == GB_ERR_CHECKSUM);

    // Non-touch status (a chip reset request) parses but reports no touch.
    build_event(b, 0, 0);
    b[0] = GB_EVENT_REQUEST;
    b[2] = GB_REQUEST_RESET;
    seal(b, GB_EVENT_HEAD_LEN);
    CHECK(gb_parse_event(&ev, b, GB_EVENT_MAX) == 0);
    CHECK(ev.status == GB_EVENT_REQUEST);
    printf("goodix event parse: ok\n");
}

// Register transport behind gb_read_reg/gb_write_reg: a flat window plus a log of the last write.
static uint8_t g_dev[0x11000];
static uint32_t g_write_reg;
static uint8_t g_write_val;
static unsigned g_write_count;

int geni_i2c_read(struct geni_i2c *s, unsigned addr7, const uint8_t *tx, size_t txlen, uint8_t *rx, size_t rxlen) {
    (void)s;
    if (addr7 != GB_I2C_ADDR || txlen != GB_REG_ADDR_LEN) return GENI_ERR_PARAM;
    uint32_t reg = ((uint32_t)tx[0] << 24) | ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
    if (reg + rxlen > sizeof(g_dev)) return GENI_ERR_PARAM;
    memcpy(rx, g_dev + reg, rxlen);
    return 0;
}

int geni_i2c_write(struct geni_i2c *s, unsigned addr7, const uint8_t *buf, size_t len, int stop) {
    (void)s; (void)stop;
    if (addr7 != GB_I2C_ADDR || len < GB_REG_ADDR_LEN) return GENI_ERR_PARAM;
    uint32_t reg = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    memcpy(g_dev + reg, buf + GB_REG_ADDR_LEN, len - GB_REG_ADDR_LEN);
    g_write_reg = reg;
    g_write_val = len > GB_REG_ADDR_LEN ? buf[GB_REG_ADDR_LEN] : 0;
    g_write_count++;
    return 0;
}

static void test_transport(void) {
    struct geni_i2c bus;
    struct gb_dev d;
    memset(&bus, 0, sizeof(bus));
    memset(&d, 0, sizeof(d));
    d.bus = &bus;
    d.addr = GB_I2C_ADDR;
    memset(g_dev, 0, sizeof(g_dev));

    // Device confirm: 8 x 0xaa written, read back identically.
    put32(g_dev + GB_REG_BOOTOPTION, 0);   // zero until the write lands, then the read compares
    CHECK(gb_dev_confirm(&d) == GB_ERR_OK);
    for (int i = 0; i < GB_DEV_CONFIRM_LEN; i++) CHECK(g_dev[GB_REG_BOOTOPTION + i] == GB_DEV_CONFIRM_VAL);

    // Firmware version record with a valid checksum.
    uint8_t ver[26];
    memset(ver, 0, sizeof(ver));
    memcpy(ver, "9916", 4);
    ver[25 - 1] = 0x00;
    seal(ver, sizeof(ver));
    memcpy(g_dev + GB_REG_FW_VERSION, ver, sizeof(ver));
    uint8_t got[26];
    CHECK(gb_read_version(&d, got) == GB_ERR_OK);
    CHECK(!memcmp(got, ver, sizeof(ver)));

    // IC info through the transport, then an event poll that must acknowledge the block.
    size_t len = build_v1(g_dev + GB_REG_IC_INFO, 0x10308, 1440, 3200);
    CHECK(len == 161);
    CHECK(gb_read_ic_info(&d) == GB_ERR_OK);
    CHECK(d.info.touch_data_addr == 0x10308);
    build_event(g_dev + 0x10308, 2, 0);
    struct gb_event ev;
    CHECK(gb_poll_event(&d, &ev) == 1);
    CHECK(ev.count == 2 && d.events == 1);
    CHECK(g_write_reg == 0x10308 && g_write_val == 0);
    // The acknowledged block now reads as idle.
    CHECK(gb_poll_event(&d, &ev) == 0);
    printf("goodix transport confirm/version/info/event: ok\n");
}

int main(void) {
    test_checksum();
    test_v1();
    test_v2();
    test_geometry_sanity();
    test_event_parse();
    test_transport();
    printf("PASS: goodix berlin protocol host tests\n");
    return 0;
}
