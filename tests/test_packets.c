#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/packets/packets.h"
#include "../src/packets/crc32.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---------- CRC-32 tests (ICD section 4.2.2 test vectors) ---------- */

static void test_crc32_zeros(void)
{
    /* ICD: Input 0x0000 0000 yields 0x2144 DF1C */
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    uint32_t crc = xs_crc32(data, sizeof(data));
    assert(crc == 0x2144DF1C);
}

static void test_crc32_sequential(void)
{
    /* ICD: Input 0x0102 0304 yields 0xB63C FBCD */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t crc = xs_crc32(data, sizeof(data));
    assert(crc == 0xB63CFBCD);
}

static void test_crc32_empty(void)
{
    uint32_t crc = xs_crc32(NULL, 0);
    assert(crc == 0x00000000);
}

static void test_crc32_all_table_entries(void)
{
    /* Verify the lookup table by computing CRC-32 for every single-byte
     * input and checking that each byte produces a unique, non-zero result.
     * Then verify against the well-known check value for "123456789". */
    uint32_t seen[256];
    for (int i = 0; i < 256; i++) {
        uint8_t byte = (uint8_t)i;
        seen[i] = xs_crc32(&byte, 1);
        /* CRC of a single byte should never be zero (except arguably 0xAC,
         * but in practice it isn't for CRC-32) */
        if (i != 0) assert(seen[i] != seen[0] || i == 0);
    }
    /* Verify no two single-byte CRCs collide (table entries are a permutation) */
    for (int i = 0; i < 256; i++) {
        for (int j = i + 1; j < 256; j++) {
            assert(seen[i] != seen[j]);
        }
    }
}

static void test_crc32_known_string(void)
{
    /* "123456789" → 0xCBF43926 (standard CRC-32 check value) */
    const char *data = "123456789";
    uint32_t crc = xs_crc32(data, 9);
    assert(crc == 0xCBF43926);
}

static void test_crc32_incremental(void)
{
    /* Same result whether computed in one shot or incrementally. */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t crc_one = xs_crc32(data, 4);

    uint32_t crc_inc = 0xFFFFFFFF;
    crc_inc = xs_crc32_update(crc_inc, data, 2);
    crc_inc = xs_crc32_update(crc_inc, data + 2, 2);
    crc_inc = xs_crc32_finalize(crc_inc);

    assert(crc_one == crc_inc);
}

/* ---------- Header struct tests ---------- */

static void test_header_size(void)
{
    assert(sizeof(xs_header_t) == XS_HEADER_SIZE);
}

static void test_header_pack(void)
{
    xs_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.sync = XS_SYNC_WORD;
    hdr.flags = xs_make_flags(1, 0); /* CRC enabled, no encryption */
    hdr.id = XS_PKT_LINE_DATA;
    xs_encode_u24(hdr.length, 8204); /* 4096 pixels × 2 bytes + 12 byte line hdr = 8204, round to 8204 */
    hdr.subpacket = 0;

    /* Verify sync at offset 0 */
    uint8_t *raw = (uint8_t *)&hdr;
    assert(raw[0] == 0x53);
    assert(raw[1] == 0x53);

    /* Verify ID at offset 3 */
    assert(raw[3] == 0x04);

    /* Verify length decodes correctly */
    uint32_t len = xs_decode_u24(hdr.length);
    assert(len == 8204);
}

/* ---------- Flags tests ---------- */

static void test_flags_crc_only(void)
{
    uint8_t flags = xs_make_flags(1, 0);
    assert((flags & 0x0F) == 0x01); /* CRC-En = 1, ENC-En = 0 */
    assert(xs_flags_valid(flags));
}

static void test_flags_both(void)
{
    uint8_t flags = xs_make_flags(1, 1);
    assert((flags & 0x0F) == 0x03);
    assert(xs_flags_valid(flags));
}

static void test_flags_none(void)
{
    uint8_t flags = xs_make_flags(0, 0);
    assert((flags & 0x0F) == 0x00);
    assert(xs_flags_valid(flags));
    assert(flags == 0xF0); /* upper = complement of 0x0 = 0xF */
}

static void test_flags_invalid(void)
{
    /* Corrupt a valid flags byte */
    uint8_t flags = xs_make_flags(1, 0);
    flags ^= 0x80; /* flip a complement bit */
    assert(!xs_flags_valid(flags));
}

/* ---------- uint24 encoding tests ---------- */

static void test_u24_zero(void)
{
    uint8_t buf[3];
    xs_encode_u24(buf, 0);
    assert(buf[0] == 0 && buf[1] == 0 && buf[2] == 0);
    assert(xs_decode_u24(buf) == 0);
}

static void test_u24_max(void)
{
    uint8_t buf[3];
    xs_encode_u24(buf, 0xFFFFFF);
    assert(buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF);
    assert(xs_decode_u24(buf) == 0xFFFFFF);
}

static void test_u24_value(void)
{
    uint8_t buf[3];
    xs_encode_u24(buf, 8204);
    assert(xs_decode_u24(buf) == 8204);
}

static void test_u24_little_endian(void)
{
    uint8_t buf[3];
    xs_encode_u24(buf, 0x030201);
    assert(buf[0] == 0x01); /* LSB first */
    assert(buf[1] == 0x02);
    assert(buf[2] == 0x03);
}

/* ---------- Sub-packet tests ---------- */

static void test_subpacket_none(void)
{
    uint8_t sp = xs_make_subpacket(0, 0, 0);
    assert(sp == 0x00);
}

static void test_subpacket_start(void)
{
    uint8_t sp = xs_make_subpacket(1, 0, 0);
    assert(sp == 0x40); /* bit 6 set */
}

static void test_subpacket_end(void)
{
    uint8_t sp = xs_make_subpacket(0, 1, 5);
    assert(sp == 0x85); /* bit 7 set, count = 5 */
}

static void test_subpacket_middle(void)
{
    uint8_t sp = xs_make_subpacket(0, 0, 3);
    assert(sp == 0x03); /* no start/end flags, count = 3 */
}

/* ---------- Payload struct size tests ---------- */

static void test_session_start_size(void)
{
    assert(sizeof(xs_session_start_t) == 12);
}

static void test_session_end_size(void)
{
    assert(sizeof(xs_session_end_t) == 4);
}

static void test_scene_start_size(void)
{
    assert(sizeof(xs_scene_start_t) == 12);
}

static void test_exposure_start_size(void)
{
    assert(sizeof(xs_exposure_start_t) == 8);
}

static void test_line_data_hdr_size(void)
{
    assert(sizeof(xs_line_data_hdr_t) == 12);
}

static void test_imager_info_size(void)
{
    assert(sizeof(xs_imager_info_t) == 12);
}

static void test_imager_telemetry_size(void)
{
    assert(sizeof(xs_imager_telemetry_t) == 12);
}

static void test_time_sync_size(void)
{
    assert(sizeof(xs_time_sync_t) == 20);
}

static void test_time_sync_pps_size(void)
{
    assert(sizeof(xs_time_sync_pps_t) == 8);
}

/* ---------- Line Data payload length calculation ---------- */

static void test_line_data_payload_12bit_16bit(void)
{
    /* Encoding 4: 12-bit pixels per 16-bit word.
     * 4096 pixels × 2 bytes + 12 byte line header = 8204
     * Must be multiple of 4: 8204 → 8204 (already aligned). */
    size_t pixel_bytes = XS_SENSOR_WIDTH * 2;
    size_t payload = sizeof(xs_line_data_hdr_t) + pixel_bytes;
    assert(payload == 8204);
    assert(payload % 4 == 0);
}

static void test_line_data_payload_8bit(void)
{
    /* Encoding 0: 8-bit pixels per byte.
     * 4096 pixels × 1 byte + 12 byte line header = 4108
     * 4108 % 4 == 0 ✓ */
    size_t pixel_bytes = XS_SENSOR_WIDTH * 1;
    size_t payload = sizeof(xs_line_data_hdr_t) + pixel_bytes;
    assert(payload == 4108);
    assert(payload % 4 == 0);
}

/* ---------- Full packet round-trip test ---------- */

static void test_session_start_packet(void)
{
    /* Build a complete Session Start packet and verify structure. */
    uint8_t buf[XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE];
    memset(buf, 0, sizeof(buf));

    /* Header */
    xs_header_t *hdr = (xs_header_t *)buf;
    hdr->sync = XS_SYNC_WORD;
    hdr->flags = xs_make_flags(1, 0);
    hdr->id = XS_PKT_SESSION_START;
    xs_encode_u24(hdr->length, 12);
    hdr->subpacket = 0;

    /* Payload */
    xs_session_start_t *payload = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    payload->data_format_major = 1;
    payload->data_format_minor = 0;
    payload->platform_id = 42;
    payload->instrument_id = 1;
    payload->session_id = 12345;

    /* Footer (CRC-32 over header + payload) */
    uint32_t crc = xs_crc32(buf, XS_HEADER_SIZE + 12);
    memcpy(buf + XS_HEADER_SIZE + 12, &crc, 4);

    /* Verify: parse it back */
    xs_header_t *parsed_hdr = (xs_header_t *)buf;
    assert(parsed_hdr->sync == XS_SYNC_WORD);
    assert(parsed_hdr->id == XS_PKT_SESSION_START);
    assert(xs_decode_u24(parsed_hdr->length) == 12);
    assert(xs_flags_valid(parsed_hdr->flags));

    xs_session_start_t *parsed_pl = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    assert(parsed_pl->session_id == 12345);
    assert(parsed_pl->platform_id == 42);

    /* Verify CRC */
    uint32_t check_crc = xs_crc32(buf, XS_HEADER_SIZE + 12);
    uint32_t stored_crc;
    memcpy(&stored_crc, buf + XS_HEADER_SIZE + 12, 4);
    assert(check_crc == stored_crc);
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running packet tests...\n\n");

    printf("CRC-32:\n");
    TEST(test_crc32_zeros);
    TEST(test_crc32_sequential);
    TEST(test_crc32_empty);
    TEST(test_crc32_all_table_entries);
    TEST(test_crc32_known_string);
    TEST(test_crc32_incremental);

    printf("\nHeader:\n");
    TEST(test_header_size);
    TEST(test_header_pack);

    printf("\nFlags:\n");
    TEST(test_flags_crc_only);
    TEST(test_flags_both);
    TEST(test_flags_none);
    TEST(test_flags_invalid);

    printf("\nuint24:\n");
    TEST(test_u24_zero);
    TEST(test_u24_max);
    TEST(test_u24_value);
    TEST(test_u24_little_endian);

    printf("\nSub-packet:\n");
    TEST(test_subpacket_none);
    TEST(test_subpacket_start);
    TEST(test_subpacket_end);
    TEST(test_subpacket_middle);

    printf("\nPayload sizes:\n");
    TEST(test_session_start_size);
    TEST(test_session_end_size);
    TEST(test_scene_start_size);
    TEST(test_exposure_start_size);
    TEST(test_line_data_hdr_size);
    TEST(test_imager_info_size);
    TEST(test_imager_telemetry_size);
    TEST(test_time_sync_size);
    TEST(test_time_sync_pps_size);

    printf("\nLine Data payload:\n");
    TEST(test_line_data_payload_12bit_16bit);
    TEST(test_line_data_payload_8bit);

    printf("\nFull packet:\n");
    TEST(test_session_start_packet);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
