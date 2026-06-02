#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/packets/packets.h"
#include "../src/packets/crc32.h"
#include "../src/packets/serializer.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---------- Packet building tests ---------- */

static void test_serialize_session_start(void)
{
    uint8_t buf[256];
    size_t len = xs_serialize_session_start(buf, sizeof(buf), 1, 0, 42, 1, 100);

    /* Total: header(8) + payload(12) + footer(4) = 24 */
    assert(len == 24);

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->sync == XS_SYNC_WORD);
    assert(hdr->id == XS_PKT_SESSION_START);
    assert(xs_decode_u24(hdr->length) == 12);
    assert(xs_flags_valid(hdr->flags));
    assert(hdr->flags & XS_FLAG_CRC_EN);

    xs_session_start_t *pl = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->platform_id == 42);
    assert(pl->instrument_id == 1);
    assert(pl->session_id == 100);

    /* Verify CRC */
    uint32_t crc = xs_crc32(buf, len - XS_FOOTER_SIZE);
    uint32_t stored;
    memcpy(&stored, buf + len - XS_FOOTER_SIZE, 4);
    assert(crc == stored);
}

static void test_serialize_session_end(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_session_end(buf, sizeof(buf), 100);

    assert(len == 16); /* 8 + 4 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_SESSION_END);
    assert(xs_decode_u24(hdr->length) == 4);

    xs_session_end_t *pl = (xs_session_end_t *)(buf + XS_HEADER_SIZE);
    assert(pl->session_id == 100);
}

static void test_serialize_scene_start(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_scene_start(buf, sizeof(buf),
                                          0, XS_SCENE_LINE_SCAN,
                                          10000, 4096);

    assert(len == 24); /* 8 + 12 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_SCENE_START);

    xs_scene_start_t *pl = (xs_scene_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->scene_number == 0);
    assert(pl->scene_type == XS_SCENE_LINE_SCAN);
    assert(xs_decode_u24(pl->scene_height) == 10000);
    assert(pl->scene_width == 4096);
}

static void test_serialize_exposure_start(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_exposure_start(buf, sizeof(buf), 123456789ULL);

    assert(len == 20); /* 8 + 8 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_EXPOSURE_START);

    xs_exposure_start_t *pl = (xs_exposure_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->imager_time == 123456789ULL);
}

static void test_serialize_line_data_small(void)
{
    /* 8 pixels, band 5, line 42, 12-bit in 16-bit encoding */
    uint16_t pixels[8] = {100, 200, 300, 400, 500, 600, 700, 800};
    uint8_t buf[256];

    size_t len = xs_serialize_line_data(buf, sizeof(buf),
                                         5, 42, 8,
                                         XS_ENC_12BIT_16BIT,
                                         pixels, 8 * sizeof(uint16_t));

    /* payload: 12 byte line hdr + 16 byte pixel data = 28 */
    /* total: 8 + 28 + 4 = 40 */
    assert(len == 40);

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_LINE_DATA);
    assert(xs_decode_u24(hdr->length) == 28);

    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    assert(lh->spectral_band == 5);
    assert(xs_decode_u24(lh->line_number) == 42);
    assert(lh->line_length == 8);
    assert(lh->format == XS_FORMAT_SINGLE_BAND);
    assert(lh->encoding == XS_ENC_12BIT_16BIT);

    /* Verify pixel data */
    uint16_t *px = (uint16_t *)(buf + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));
    assert(px[0] == 100);
    assert(px[7] == 800);
}

static void test_serialize_line_data_full_width(void)
{
    /* Full sensor width: 4096 pixels */
    uint16_t *pixels = calloc(XS_SENSOR_WIDTH, sizeof(uint16_t));
    assert(pixels);
    pixels[0] = 1;
    pixels[4095] = 4095;

    size_t buf_size = XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t)
                    + XS_SENSOR_WIDTH * 2 + XS_FOOTER_SIZE;
    uint8_t *buf = malloc(buf_size);
    assert(buf);

    size_t len = xs_serialize_line_data(buf, buf_size,
                                         0, 0, XS_SENSOR_WIDTH,
                                         XS_ENC_12BIT_16BIT,
                                         pixels, XS_SENSOR_WIDTH * 2);

    /* payload: 12 + 8192 = 8204 */
    assert(len == XS_HEADER_SIZE + 8204 + XS_FOOTER_SIZE);

    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    assert(lh->line_length == XS_SENSOR_WIDTH);

    uint16_t *px = (uint16_t *)(buf + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));
    assert(px[0] == 1);
    assert(px[4095] == 4095);

    /* Verify CRC */
    uint32_t crc = xs_crc32(buf, len - XS_FOOTER_SIZE);
    uint32_t stored;
    memcpy(&stored, buf + len - XS_FOOTER_SIZE, 4);
    assert(crc == stored);

    free(pixels);
    free(buf);
}

static void test_serialize_imager_telemetry(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_imager_telemetry(buf, sizeof(buf),
                                                500000ULL, 25);

    assert(len == 24); /* 8 + 12 + 4 */

    xs_header_t *hdr = (xs_header_t *)buf;
    assert(hdr->id == XS_PKT_IMAGER_TELEMETRY);

    xs_imager_telemetry_t *pl = (xs_imager_telemetry_t *)(buf + XS_HEADER_SIZE);
    assert(pl->imager_time == 500000ULL);
    assert(pl->sensor_temperature == 25);
}

static void test_serialize_payload_alignment(void)
{
    /* ICD requires payload length to be multiple of 4.
     * Session End payload is 4 bytes -- already aligned. */
    uint8_t buf[64];
    size_t len = xs_serialize_session_end(buf, sizeof(buf), 1);

    xs_header_t *hdr = (xs_header_t *)buf;
    uint32_t payload_len = xs_decode_u24(hdr->length);
    assert(payload_len % 4 == 0);
    (void)len;
}

static void test_serialize_buffer_too_small(void)
{
    uint8_t buf[4]; /* way too small */
    size_t len = xs_serialize_session_start(buf, sizeof(buf), 1, 0, 0, 0, 0);
    assert(len == 0); /* should fail gracefully */
}

/* ---------- CRC integrity across full packet ---------- */

static void test_serialize_crc_covers_header_and_payload(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_session_end(buf, sizeof(buf), 42);

    /* CRC is computed over header + payload */
    uint32_t expected_crc = xs_crc32(buf, XS_HEADER_SIZE + 4);
    uint32_t stored_crc;
    memcpy(&stored_crc, buf + len - XS_FOOTER_SIZE, 4);
    assert(expected_crc == stored_crc);

    /* Corrupt one byte and verify CRC no longer matches */
    buf[XS_HEADER_SIZE] ^= 0xFF;
    uint32_t bad_crc = xs_crc32(buf, XS_HEADER_SIZE + 4);
    assert(bad_crc != stored_crc);
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running serializer tests...\n\n");

    printf("Packet serialization:\n");
    TEST(test_serialize_session_start);
    TEST(test_serialize_session_end);
    TEST(test_serialize_scene_start);
    TEST(test_serialize_exposure_start);
    TEST(test_serialize_line_data_small);
    TEST(test_serialize_line_data_full_width);
    TEST(test_serialize_imager_telemetry);

    printf("\nConstraints:\n");
    TEST(test_serialize_payload_alignment);
    TEST(test_serialize_buffer_too_small);
    TEST(test_serialize_crc_covers_header_and_payload);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
