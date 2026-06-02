#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/packets/packets.h"
#include "../src/packets/crc32.h"
#include "../src/packets/serializer.h"
#include "../src/packets/pkt_parser.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---------- Sync detection ---------- */

static void test_parser_find_sync(void)
{
    /* Garbage bytes then a valid sync word */
    uint8_t stream[] = {0xAA, 0xBB, 0xCC, 0x53, 0x53, 0x00, 0x00};
    int offset = xs_find_sync(stream, sizeof(stream));
    assert(offset == 3);
}

static void test_parser_find_sync_at_start(void)
{
    uint8_t stream[] = {0x53, 0x53, 0x01, 0x00};
    int offset = xs_find_sync(stream, sizeof(stream));
    assert(offset == 0);
}

static void test_parser_find_sync_none(void)
{
    uint8_t stream[] = {0xAA, 0xBB, 0xCC, 0xDD};
    int offset = xs_find_sync(stream, sizeof(stream));
    assert(offset == -1);
}

static void test_parser_find_sync_at_end(void)
{
    uint8_t stream[] = {0xAA, 0xBB, 0x53, 0x53};
    int offset = xs_find_sync(stream, sizeof(stream));
    assert(offset == 2);
}

/* ---------- Header parsing ---------- */

static void test_parser_parse_header(void)
{
    uint8_t buf[64];
    xs_serialize_session_end(buf, sizeof(buf), 42);

    xs_parsed_header_t parsed;
    int rc = xs_parse_header(buf, sizeof(buf), &parsed);
    assert(rc == 0);
    assert(parsed.id == XS_PKT_SESSION_END);
    assert(parsed.payload_len == 4);
    assert(parsed.has_crc == 1);
    assert(parsed.is_encrypted == 0);
    assert(parsed.total_len == 16); /* 8 + 4 + 4 */
}

static void test_parser_parse_header_bad_sync(void)
{
    uint8_t buf[64];
    xs_serialize_session_end(buf, sizeof(buf), 42);
    buf[0] = 0x00; /* corrupt sync */

    xs_parsed_header_t parsed;
    int rc = xs_parse_header(buf, sizeof(buf), &parsed);
    assert(rc == XS_ERR_BAD_SYNC);
}

static void test_parser_parse_header_bad_flags(void)
{
    uint8_t buf[64];
    xs_serialize_session_end(buf, sizeof(buf), 42);
    buf[2] = 0xFF; /* corrupt flags: complement check fails */

    xs_parsed_header_t parsed;
    int rc = xs_parse_header(buf, sizeof(buf), &parsed);
    assert(rc == XS_ERR_BAD_FLAGS);
}

static void test_parser_parse_header_truncated(void)
{
    uint8_t buf[4] = {0x53, 0x53, 0x01, 0x00};

    xs_parsed_header_t parsed;
    int rc = xs_parse_header(buf, sizeof(buf), &parsed);
    assert(rc == XS_ERR_TRUNCATED);
}

/* ---------- CRC validation ---------- */

static void test_parser_validate_crc(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_session_end(buf, sizeof(buf), 42);

    int rc = xs_validate_crc(buf, len);
    assert(rc == 0);
}

static void test_parser_validate_crc_corrupted(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_session_end(buf, sizeof(buf), 42);

    /* Corrupt a payload byte */
    buf[XS_HEADER_SIZE] ^= 0x01;

    int rc = xs_validate_crc(buf, len);
    assert(rc == XS_ERR_BAD_CRC);
}

/* ---------- Round-trip tests: serialize → parse ---------- */

static void test_roundtrip_session_start(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_session_start(buf, sizeof(buf), 2, 1, 99, 7, 54321);
    assert(len > 0);

    xs_parsed_header_t hdr;
    assert(xs_parse_header(buf, len, &hdr) == 0);
    assert(hdr.id == XS_PKT_SESSION_START);
    assert(xs_validate_crc(buf, len) == 0);

    xs_session_start_t *pl = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->data_format_major == 2);
    assert(pl->data_format_minor == 1);
    assert(pl->platform_id == 99);
    assert(pl->instrument_id == 7);
    assert(pl->session_id == 54321);
}

static void test_roundtrip_line_data(void)
{
    uint16_t pixels[16];
    for (int i = 0; i < 16; i++) pixels[i] = i * 100;

    uint8_t buf[256];
    size_t len = xs_serialize_line_data(buf, sizeof(buf),
                                         3, 99, 16,
                                         XS_ENC_12BIT_16BIT,
                                         pixels, 16 * 2);
    assert(len > 0);

    xs_parsed_header_t hdr;
    assert(xs_parse_header(buf, len, &hdr) == 0);
    assert(hdr.id == XS_PKT_LINE_DATA);
    assert(xs_validate_crc(buf, len) == 0);

    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    assert(lh->spectral_band == 3);
    assert(xs_decode_u24(lh->line_number) == 99);
    assert(lh->line_length == 16);

    uint16_t *px = (uint16_t *)(buf + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));
    for (int i = 0; i < 16; i++) {
        assert(px[i] == (uint16_t)(i * 100));
    }
}

static void test_roundtrip_exposure_start(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_exposure_start(buf, sizeof(buf), 9876543210ULL);
    assert(len > 0);

    xs_parsed_header_t hdr;
    assert(xs_parse_header(buf, len, &hdr) == 0);
    assert(hdr.id == XS_PKT_EXPOSURE_START);
    assert(xs_validate_crc(buf, len) == 0);

    xs_exposure_start_t *pl = (xs_exposure_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->imager_time == 9876543210ULL);
}

static void test_roundtrip_scene_start(void)
{
    uint8_t buf[64];
    size_t len = xs_serialize_scene_start(buf, sizeof(buf),
                                           0, XS_SCENE_LINE_SCAN,
                                           8936, 4096);
    assert(len > 0);

    xs_parsed_header_t hdr;
    assert(xs_parse_header(buf, len, &hdr) == 0);
    assert(xs_validate_crc(buf, len) == 0);

    xs_scene_start_t *pl = (xs_scene_start_t *)(buf + XS_HEADER_SIZE);
    assert(pl->scene_type == XS_SCENE_LINE_SCAN);
    assert(xs_decode_u24(pl->scene_height) == 8936);
    assert(pl->scene_width == 4096);
}

/* ---------- Multi-packet stream parsing ---------- */

static void test_parse_stream_two_packets(void)
{
    uint8_t stream[128];
    size_t pos = 0;

    /* First packet: session start */
    size_t len1 = xs_serialize_session_start(stream + pos, sizeof(stream) - pos,
                                              1, 0, 1, 1, 1);
    assert(len1 > 0);
    pos += len1;

    /* Second packet: session end */
    size_t len2 = xs_serialize_session_end(stream + pos, sizeof(stream) - pos, 1);
    assert(len2 > 0);
    pos += len2;

    /* Parse first packet */
    xs_parsed_header_t hdr1;
    assert(xs_parse_header(stream, pos, &hdr1) == 0);
    assert(hdr1.id == XS_PKT_SESSION_START);
    assert(xs_validate_crc(stream, hdr1.total_len) == 0);

    /* Parse second packet */
    xs_parsed_header_t hdr2;
    assert(xs_parse_header(stream + hdr1.total_len, pos - hdr1.total_len, &hdr2) == 0);
    assert(hdr2.id == XS_PKT_SESSION_END);
    assert(xs_validate_crc(stream + hdr1.total_len, hdr2.total_len) == 0);
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running parser tests...\n\n");

    printf("Sync detection:\n");
    TEST(test_parser_find_sync);
    TEST(test_parser_find_sync_at_start);
    TEST(test_parser_find_sync_none);
    TEST(test_parser_find_sync_at_end);

    printf("\nHeader parsing:\n");
    TEST(test_parser_parse_header);
    TEST(test_parser_parse_header_bad_sync);
    TEST(test_parser_parse_header_bad_flags);
    TEST(test_parser_parse_header_truncated);

    printf("\nCRC validation:\n");
    TEST(test_parser_validate_crc);
    TEST(test_parser_validate_crc_corrupted);

    printf("\nRound-trip:\n");
    TEST(test_roundtrip_session_start);
    TEST(test_roundtrip_line_data);
    TEST(test_roundtrip_exposure_start);
    TEST(test_roundtrip_scene_start);

    printf("\nStream parsing:\n");
    TEST(test_parse_stream_two_packets);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
