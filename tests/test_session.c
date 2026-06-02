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

/* Helper: create a small synthetic scene (n_lines × width × n_bands) */
static uint16_t *make_scene(int n_lines, int width, int n_bands)
{
    uint16_t *data = calloc(n_lines * width * n_bands, sizeof(uint16_t));
    assert(data);
    for (int line = 0; line < n_lines; line++) {
        for (int band = 0; band < n_bands; band++) {
            for (int px = 0; px < width; px++) {
                int idx = (line * width * n_bands) + (px * n_bands) + band;
                data[idx] = (uint16_t)((line * 100 + band * 10 + px) & 0x0FFF);
            }
        }
    }
    return data;
}

/* Helper: walk a session stream and count packets by type */
typedef struct {
    int session_start;
    int session_end;
    int scene_start;
    int exposure_start;
    int line_data;
    int imager_telemetry;
    int other;
    int crc_errors;
    int total;
} pkt_counts_t;

static pkt_counts_t count_packets(const uint8_t *stream, size_t stream_len)
{
    pkt_counts_t c = {0};
    size_t pos = 0;

    while (pos < stream_len) {
        int sync_off = xs_find_sync(stream + pos, stream_len - pos);
        if (sync_off < 0) break;
        pos += sync_off;

        xs_parsed_header_t hdr;
        if (xs_parse_header(stream + pos, stream_len - pos, &hdr) != 0) break;
        if (pos + hdr.total_len > stream_len) break;

        if (xs_validate_crc(stream + pos, hdr.total_len) != 0)
            c.crc_errors++;

        switch (hdr.id) {
            case XS_PKT_SESSION_START:    c.session_start++; break;
            case XS_PKT_SESSION_END:      c.session_end++; break;
            case XS_PKT_SCENE_START:      c.scene_start++; break;
            case XS_PKT_EXPOSURE_START:   c.exposure_start++; break;
            case XS_PKT_LINE_DATA:        c.line_data++; break;
            case XS_PKT_IMAGER_TELEMETRY: c.imager_telemetry++; break;
            default:                      c.other++; break;
        }
        c.total++;
        pos += hdr.total_len;
    }
    return c;
}

/* ---------- Session structure tests ---------- */

static void test_session_starts_with_session_start(void)
{
    xs_session_cfg_t cfg = {
        .session_id = 1,
        .platform_id = 42,
        .instrument_id = 1,
        .n_bands = 3,
        .n_lines = 2,
        .width = 8,
        .band_wavelengths = {500, 600, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(2, 8, 3);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);
    assert(buf);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    assert(len > 0);

    xs_parsed_header_t hdr;
    assert(xs_parse_header(buf, len, &hdr) == 0);
    assert(hdr.id == XS_PKT_SESSION_START);

    xs_session_start_t *pl = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->session_id == 1);
    assert(pl->platform_id == 42);

    free(scene);
    free(buf);
}

static void test_session_ends_with_session_end(void)
{
    xs_session_cfg_t cfg = {
        .session_id = 77,
        .n_bands = 2,
        .n_lines = 1,
        .width = 4,
        .band_wavelengths = {500, 600},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(1, 4, 2);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);
    assert(buf);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    assert(len > 0);

    /* Find the last packet */
    size_t pos = 0;
    size_t last_pos = 0;
    while (pos < len) {
        xs_parsed_header_t hdr;
        if (xs_parse_header(buf + pos, len - pos, &hdr) != 0) break;
        if (pos + hdr.total_len > len) break;
        last_pos = pos;
        pos += hdr.total_len;
    }

    xs_parsed_header_t last_hdr;
    assert(xs_parse_header(buf + last_pos, len - last_pos, &last_hdr) == 0);
    assert(last_hdr.id == XS_PKT_SESSION_END);

    xs_session_end_t *pl = (xs_session_end_t *)(buf + last_pos + XS_HEADER_SIZE);
    assert(pl->session_id == 77);

    free(scene);
    free(buf);
}

static void test_session_has_scene_start(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 3,
        .width = 8,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(3, 8, 2);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.scene_start == 1);

    /* Find the scene start and verify dimensions */
    size_t pos = 0;
    while (pos < len) {
        xs_parsed_header_t hdr;
        xs_parse_header(buf + pos, len - pos, &hdr);
        if (hdr.id == XS_PKT_SCENE_START) {
            xs_scene_start_t *sc = (xs_scene_start_t *)(buf + pos + XS_HEADER_SIZE);
            assert(sc->scene_type == XS_SCENE_LINE_SCAN);
            assert(xs_decode_u24(sc->scene_height) == 3);
            assert(sc->scene_width == 8);
            break;
        }
        pos += hdr.total_len;
    }

    free(scene);
    free(buf);
}

/* ---------- Packet count tests ---------- */

static void test_session_packet_counts_1line_2bands(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 1,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(1, 4, 2);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.session_start == 1);
    assert(c.session_end == 1);
    assert(c.scene_start == 1);
    assert(c.exposure_start == 1);  /* 1 per line */
    assert(c.line_data == 2);       /* 1 per band per line */
    assert(c.crc_errors == 0);

    free(scene);
    free(buf);
}

static void test_session_packet_counts_5lines_3bands(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 3,
        .n_lines = 5,
        .width = 8,
        .band_wavelengths = {500, 600, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(5, 8, 3);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.session_start == 1);
    assert(c.session_end == 1);
    assert(c.scene_start == 1);
    assert(c.exposure_start == 5);   /* 1 per line */
    assert(c.line_data == 15);       /* 3 bands × 5 lines */
    assert(c.crc_errors == 0);

    free(scene);
    free(buf);
}

/* ---------- CRC integrity ---------- */

static void test_session_all_packets_have_valid_crc(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 4,
        .n_lines = 10,
        .width = 16,
        .band_wavelengths = {500, 600, 700, 800},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t *scene = make_scene(10, 16, 4);
    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);
    pkt_counts_t c = count_packets(buf, len);

    assert(c.total > 0);
    assert(c.crc_errors == 0);

    free(scene);
    free(buf);
}

/* ---------- Pixel data integrity ---------- */

static void test_session_pixel_data_roundtrip(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 1,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    /* Known pixel values: pixel-interleaved (px0_b0, px0_b1, px1_b0, px1_b1, ...) */
    uint16_t scene[] = {
        100, 200,   /* px 0: band0=100, band1=200 */
        300, 400,   /* px 1 */
        500, 600,   /* px 2 */
        700, 800,   /* px 3 */
    };

    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);

    /* Find the LINE DATA packets and verify pixel values */
    size_t pos = 0;
    int line_data_count = 0;
    while (pos < len) {
        xs_parsed_header_t hdr;
        if (xs_parse_header(buf + pos, len - pos, &hdr) != 0) break;
        if (hdr.id == XS_PKT_LINE_DATA) {
            xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + pos + XS_HEADER_SIZE);
            uint16_t *px = (uint16_t *)(buf + pos + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));

            if (lh->spectral_band == 0) {
                assert(px[0] == 100);
                assert(px[1] == 300);
                assert(px[2] == 500);
                assert(px[3] == 700);
            } else if (lh->spectral_band == 1) {
                assert(px[0] == 200);
                assert(px[1] == 400);
                assert(px[2] == 600);
                assert(px[3] == 800);
            }
            line_data_count++;
        }
        pos += hdr.total_len;
    }
    assert(line_data_count == 2);

    free(buf);
}

/* ---------- Band selection ---------- */

static void test_session_band_subset(void)
{
    /* 4 bands available but only 2 enabled */
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 1,
        .width = 4,
        .band_wavelengths = {500, 700}, /* only bands 0 and 2 of a 4-band scene */
        .band_indices = {0, 2},
        .source_n_bands = 4,
        .encoding = XS_ENC_12BIT_16BIT,
    };

    /* Source scene has 4 bands per pixel */
    uint16_t scene[] = {
        10, 20, 30, 40,   /* px 0: b0=10, b1=20, b2=30, b3=40 */
        50, 60, 70, 80,   /* px 1 */
        90,100,110,120,   /* px 2 */
       130,140,150,160,   /* px 3 */
    };

    size_t buf_size = xs_session_size(&cfg);
    uint8_t *buf = malloc(buf_size);

    size_t len = xs_build_session(buf, buf_size, &cfg, scene);

    /* Verify only 2 LINE DATA packets and correct band data */
    size_t pos = 0;
    int ld_count = 0;
    while (pos < len) {
        xs_parsed_header_t hdr;
        if (xs_parse_header(buf + pos, len - pos, &hdr) != 0) break;
        if (hdr.id == XS_PKT_LINE_DATA) {
            xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + pos + XS_HEADER_SIZE);
            uint16_t *px = (uint16_t *)(buf + pos + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));

            if (lh->spectral_band == 0) {
                /* Band index 0 from source */
                assert(px[0] == 10);
                assert(px[1] == 50);
                assert(px[2] == 90);
                assert(px[3] == 130);
            } else if (lh->spectral_band == 1) {
                /* Band index 2 from source */
                assert(px[0] == 30);
                assert(px[1] == 70);
                assert(px[2] == 110);
                assert(px[3] == 150);
            }
            ld_count++;
        }
        pos += hdr.total_len;
    }
    assert(ld_count == 2);

    free(buf);
}

/* ---------- Edge cases ---------- */

static void test_session_size_estimate(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 31,
        .n_lines = 100,
        .width = 4096,
        .encoding = XS_ENC_12BIT_16BIT,
    };

    size_t est = xs_session_size(&cfg);
    /* Per line: 1 exposure_start + 31 line_data packets
     * Each line_data: 8 + (12 + 4096*2) + 4 = 8216 + 4 = 8220 (padded payload: 8204)
     *   actually: header(8) + payload(12 + 8192 = 8204) + footer(4) = 8216
     * Each exposure_start: 8 + 8 + 4 = 20
     * Per line total: 20 + 31 * 8216 = 254716
     * 100 lines: 25,471,600
     * Plus session_start(24) + scene_start(24) + session_end(16) = 64
     * Total ≈ 25,471,664
     */
    assert(est > 25000000);
    assert(est < 26000000);
}

static void test_session_buffer_too_small(void)
{
    xs_session_cfg_t cfg = {
        .n_bands = 2,
        .n_lines = 1,
        .width = 4,
        .band_wavelengths = {500, 700},
        .encoding = XS_ENC_12BIT_16BIT,
    };

    uint16_t scene[] = {1,2, 3,4, 5,6, 7,8};
    size_t len = xs_build_session(NULL, 0, &cfg, scene);
    assert(len == 0);
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running session builder tests...\n\n");

    printf("Session structure:\n");
    TEST(test_session_starts_with_session_start);
    TEST(test_session_ends_with_session_end);
    TEST(test_session_has_scene_start);

    printf("\nPacket counts:\n");
    TEST(test_session_packet_counts_1line_2bands);
    TEST(test_session_packet_counts_5lines_3bands);

    printf("\nCRC integrity:\n");
    TEST(test_session_all_packets_have_valid_crc);

    printf("\nPixel data:\n");
    TEST(test_session_pixel_data_roundtrip);

    printf("\nBand selection:\n");
    TEST(test_session_band_subset);

    printf("\nEdge cases:\n");
    TEST(test_session_size_estimate);
    TEST(test_session_buffer_too_small);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
