#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/packets/packets.h"
#include "../src/packets/crc32.h"
#include "../src/packets/serializer.h"
#include "../src/packets/pkt_parser.h"
#include "../src/packets/session.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---------- Packet counting helper ---------- */

typedef struct {
    int session_start;
    int session_end;
    int scene_start;
    int exposure_start;
    int line_data;
    int thumbnail_data;
    int time_sync;
    int imager_info;
    int imager_config;
    int imager_telemetry;
    int total;
    int crc_errors;
} pkt_counts_t;

static pkt_counts_t count_packets(const uint8_t *stream, size_t len)
{
    pkt_counts_t c = {0};
    size_t pos = 0;
    while (pos < len) {
        int sync_off = xs_find_sync(stream + pos, len - pos);
        if (sync_off < 0) break;
        pos += sync_off;

        xs_parsed_header_t hdr;
        if (xs_parse_header(stream + pos, len - pos, &hdr) != 0) break;
        if (pos + hdr.total_len > len) break;

        if (xs_validate_crc(stream + pos, hdr.total_len) != 0)
            c.crc_errors++;

        switch (hdr.id) {
            case XS_PKT_SESSION_START:    c.session_start++; break;
            case XS_PKT_SESSION_END:      c.session_end++; break;
            case XS_PKT_SCENE_START:      c.scene_start++; break;
            case XS_PKT_EXPOSURE_START:   c.exposure_start++; break;
            case XS_PKT_LINE_DATA:        c.line_data++; break;
            case XS_PKT_THUMBNAIL_DATA:   c.thumbnail_data++; break;
            case XS_PKT_TIME_SYNC:        c.time_sync++; break;
            case XS_PKT_IMAGER_INFO:      c.imager_info++; break;
            case XS_PKT_IMAGER_CONFIG:    c.imager_config++; break;
            case XS_PKT_IMAGER_TELEMETRY: c.imager_telemetry++; break;
        }
        c.total++;
        pos += hdr.total_len;
    }
    return c;
}

/* Helper: find nth packet of a given type and return pointer to it */
static const uint8_t *find_nth_packet(const uint8_t *stream, size_t len,
                                       uint8_t pkt_id, int n)
{
    size_t pos = 0;
    int count = 0;
    while (pos < len) {
        int sync_off = xs_find_sync(stream + pos, len - pos);
        if (sync_off < 0) return NULL;
        pos += sync_off;

        xs_parsed_header_t hdr;
        if (xs_parse_header(stream + pos, len - pos, &hdr) != 0) return NULL;
        if (pos + hdr.total_len > len) return NULL;

        if (hdr.id == pkt_id) {
            if (count == n) return stream + pos;
            count++;
        }
        pos += hdr.total_len;
    }
    return NULL;
}

/* Helper: get packet order (which packet index is this type first seen at) */
static int packet_first_index(const uint8_t *stream, size_t len, uint8_t pkt_id)
{
    size_t pos = 0;
    int idx = 0;
    while (pos < len) {
        int sync_off = xs_find_sync(stream + pos, len - pos);
        if (sync_off < 0) return -1;
        pos += sync_off;

        xs_parsed_header_t hdr;
        if (xs_parse_header(stream + pos, len - pos, &hdr) != 0) return -1;
        if (pos + hdr.total_len > len) return -1;

        if (hdr.id == pkt_id) return idx;
        pos += hdr.total_len;
        idx++;
    }
    return -1;
}

/* ---------- New serializer tests ---------- */

static void test_serialize_imager_info(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_imager_info(buf, sizeof(buf),
                                           0x123456, 789, 2, 1, 3, 0, 42);
    assert(len == 24); /* 8 + 12 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_IMAGER_INFO);
    assert(xs_decode_u24(hdr->length) == 12);

    xs_imager_info_t *pl = (xs_imager_info_t *)(buf + XS_HEADER_SIZE);
    assert(xs_decode_u24(pl->imager_product_id) == 0x123456);
    assert(pl->imager_serial == 789);
    assert(pl->fw_major == 2);
    assert(pl->fw_minor == 1);
    assert(pl->sw_major == 3);
    assert(pl->sw_minor == 0);
    assert(pl->baseline_number == 42);

    assert(xs_validate_crc(buf, len) == 0);
}

static void test_serialize_imager_config(void)
{
    uint16_t wavelengths[] = {500, 600, 700};
    uint8_t band_setup[] = {1, 2, 1}; /* band 0: no TDI, band 1: 2 TDI stages, band 2: no TDI */
    uint16_t band_start_rows[] = {100, 200, 300};

    uint8_t buf[256];
    size_t len = xs_serialize_imager_config(buf, sizeof(buf),
                                             1000, 3, 500,
                                             band_setup, 0, 0,
                                             band_start_rows, wavelengths, 0);
    assert(len > 0);

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_IMAGER_CONFIG);
    assert(xs_validate_crc(buf, len) == 0);

    /* Verify payload length is multiple of 4 */
    assert(xs_decode_u24(hdr->length) % 4 == 0);
}

static void test_serialize_time_sync(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_time_sync(buf, sizeof(buf),
                                         123456ULL, 1, 789012ULL);
    assert(len == 32); /* 8 + 20 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_TIME_SYNC);

    xs_time_sync_t *pl = (xs_time_sync_t *)(buf + XS_HEADER_SIZE);
    assert(pl->imager_time == 123456ULL);
    assert(pl->time_format == 1);
    assert(pl->platform_time == 789012ULL);

    assert(xs_validate_crc(buf, len) == 0);
}

static void test_serialize_thumbnail_line_data(void)
{
    /* Thumbnail with 4x reduction: 4096/4 = 1024 pixels */
    uint16_t pixels[1024];
    for (int i = 0; i < 1024; i++) pixels[i] = (uint16_t)i;

    uint8_t buf[4096];
    size_t len = xs_serialize_thumbnail_line_data(buf, sizeof(buf),
                                                   0, 42, 1024,
                                                   pixels, 1024 * 2);
    assert(len > 0);

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_THUMBNAIL_DATA);

    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    assert(lh->spectral_band == 0);
    assert(xs_decode_u24(lh->line_number) == 42);
    assert(lh->line_length == 1024);
    assert(lh->encoding == XS_ENC_8BIT); /* thumbnails always 8-bit per ICD */

    assert(xs_validate_crc(buf, len) == 0);
}

/* ---------- Session structure tests ---------- */

static void test_session_has_imager_info(void)
{
    xs_session_cfg_t cfg = {
        .session_id = 1,
        .platform_id = 42,
        .instrument_id = 1,
        .n_bands = 2,
        .n_lines = 2,
        .width = 8,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = calloc(2 * 8 * 2, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.imager_info == 1);

    /* Imager Info should be the 2nd packet (after Session Start) */
    int idx = packet_first_index(buf, len, XS_PKT_IMAGER_INFO);
    assert(idx == 1);

    free(scene);
    free(buf);
}

static void test_session_has_imager_config(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 3,
        .n_lines = 2,
        .width = 8,
        .band_wavelengths = {500, 600, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = calloc(2 * 8 * 3, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.imager_config == 1);

    /* Imager Config should appear before Scene Start */
    int cfg_idx = packet_first_index(buf, len, XS_PKT_IMAGER_CONFIG);
    int scene_idx = packet_first_index(buf, len, XS_PKT_SCENE_START);
    assert(cfg_idx < scene_idx);

    /* Verify it contains the correct band count */
    const uint8_t *pkt = find_nth_packet(buf, len, XS_PKT_IMAGER_CONFIG, 0);
    assert(pkt);
    xs_imager_config_fixed_t *icfg = (xs_imager_config_fixed_t *)(pkt + XS_HEADER_SIZE);
    assert(icfg->spectral_bands == 3);

    free(scene);
    free(buf);
}

static void test_session_has_time_sync(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 2,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = calloc(2 * 4 * 2, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    /* At least one time sync at session start */
    assert(c.time_sync >= 1);

    /* Time Sync should appear before Scene Start */
    int ts_idx = packet_first_index(buf, len, XS_PKT_TIME_SYNC);
    int scene_idx = packet_first_index(buf, len, XS_PKT_SCENE_START);
    assert(ts_idx < scene_idx);

    free(scene);
    free(buf);
}

static void test_session_has_imager_telemetry(void)
{
    /* With enough lines, telemetry should be injected.
     * At 100 lines with ~1ms line period, we have 100ms of capture time.
     * Telemetry every 500ms means 0 during capture for short scenes.
     * Use line_period_us to simulate longer periods. */
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 10,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
        .line_period_us = 100000, /* 100ms per line → 1s total → 2 telemetry packets */
    };

    uint16_t *scene = calloc(10 * 4 * 2, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.imager_telemetry >= 1);

    free(scene);
    free(buf);
}

static void test_session_full_packet_order(void)
{
    xs_session_cfg_t cfg = {
        .session_id = 1,
        .n_bands = 2,
        .n_lines = 2,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = calloc(2 * 4 * 2, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);

    /* Expected order:
     * 0: SESSION_START
     * 1: IMAGER_INFO
     * 2: IMAGER_CONFIG
     * 3: TIME_SYNC
     * 4: SCENE_START
     * 5+: EXPOSURE_START + LINE_DATA packets...
     * last: SESSION_END
     */
    int ss_idx = packet_first_index(buf, len, XS_PKT_SESSION_START);
    int ii_idx = packet_first_index(buf, len, XS_PKT_IMAGER_INFO);
    int ic_idx = packet_first_index(buf, len, XS_PKT_IMAGER_CONFIG);
    int ts_idx = packet_first_index(buf, len, XS_PKT_TIME_SYNC);
    int sc_idx = packet_first_index(buf, len, XS_PKT_SCENE_START);
    int es_idx = packet_first_index(buf, len, XS_PKT_EXPOSURE_START);

    assert(ss_idx == 0);
    assert(ii_idx > ss_idx);
    assert(ic_idx > ii_idx);
    assert(ts_idx > ic_idx);
    assert(sc_idx > ts_idx);
    assert(es_idx > sc_idx);

    /* Session End is last */
    size_t pos = 0;
    uint8_t last_id = 0;
    while (pos < len) {
        xs_parsed_header_t hdr;
        if (xs_parse_header(buf + pos, len - pos, &hdr) != 0) break;
        if (pos + hdr.total_len > len) break;
        last_id = hdr.id;
        pos += hdr.total_len;
    }
    assert(last_id == XS_PKT_SESSION_END);

    free(scene);
    free(buf);
}

static void test_session_all_crcs_valid(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 3,
        .n_lines = 5,
        .width = 8,
        .band_wavelengths = {500, 600, 700},
        .encoding = XS_ENC_12BIT_16BIT,
        .line_period_us = 200000, /* inject telemetry */
    };

    uint16_t *scene = calloc(5 * 8 * 3, sizeof(uint16_t));
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.total > 0);
    assert(c.crc_errors == 0);

    free(scene);
    free(buf);
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running full-fidelity tests...\n\n");

    printf("New serializers:\n");
    TEST(test_serialize_imager_info);
    TEST(test_serialize_imager_config);
    TEST(test_serialize_time_sync);
    TEST(test_serialize_thumbnail_line_data);

    printf("\nSession structure:\n");
    TEST(test_session_has_imager_info);
    TEST(test_session_has_imager_config);
    TEST(test_session_has_time_sync);
    TEST(test_session_has_imager_telemetry);
    TEST(test_session_full_packet_order);
    TEST(test_session_all_crcs_valid);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
