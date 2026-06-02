#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/packets/packets.h"
#include "../src/emu/control.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ---------- Command serialization (client side) ---------- */

static void test_serialize_reconfig_cmd(void)
{
    xs_reconfig_cmd_t cmd = {
        .n_bands = 3,
        .band_wavelengths = {500, 600, 700},
    };

    uint8_t buf[256];
    size_t len = xs_serialize_reconfig(buf, sizeof(buf), &cmd);
    assert(len > 0);

    /* Starts with magic */
    assert(buf[0] == 'R');
    assert(buf[1] == 'C');
    assert(buf[2] == 'F');
    assert(buf[3] == 'G');
}

static void test_serialize_reconfig_cmd_max_bands(void)
{
    xs_reconfig_cmd_t cmd = { .n_bands = XS_MAX_BANDS };
    for (int i = 0; i < XS_MAX_BANDS; i++)
        cmd.band_wavelengths[i] = 445 + i * 14;

    uint8_t buf[512];
    size_t len = xs_serialize_reconfig(buf, sizeof(buf), &cmd);
    assert(len > 0);
}

/* ---------- Command parsing (emulator side) ---------- */

static void test_parse_reconfig_cmd(void)
{
    xs_reconfig_cmd_t cmd_out = {
        .n_bands = 3,
        .band_wavelengths = {500, 600, 700},
    };

    uint8_t buf[256];
    size_t len = xs_serialize_reconfig(buf, sizeof(buf), &cmd_out);
    assert(len > 0);

    xs_reconfig_cmd_t cmd_in;
    int rc = xs_parse_reconfig(buf, len, &cmd_in);
    assert(rc == 0);
    assert(cmd_in.n_bands == 3);
    assert(cmd_in.band_wavelengths[0] == 500);
    assert(cmd_in.band_wavelengths[1] == 600);
    assert(cmd_in.band_wavelengths[2] == 700);
}

static void test_parse_reconfig_roundtrip_all_bands(void)
{
    xs_reconfig_cmd_t cmd_out = { .n_bands = 5 };
    uint16_t wl[] = {445, 550, 650, 750, 869};
    memcpy(cmd_out.band_wavelengths, wl, sizeof(wl));

    uint8_t buf[256];
    size_t len = xs_serialize_reconfig(buf, sizeof(buf), &cmd_out);

    xs_reconfig_cmd_t cmd_in;
    assert(xs_parse_reconfig(buf, len, &cmd_in) == 0);
    assert(cmd_in.n_bands == 5);
    for (int i = 0; i < 5; i++)
        assert(cmd_in.band_wavelengths[i] == wl[i]);
}

static void test_parse_reconfig_bad_magic(void)
{
    uint8_t buf[] = {'X', 'X', 'X', 'X', 0x01, 0xF4, 0x01};

    xs_reconfig_cmd_t cmd;
    int rc = xs_parse_reconfig(buf, sizeof(buf), &cmd);
    assert(rc != 0);
}

static void test_parse_reconfig_truncated(void)
{
    uint8_t buf[] = {'R', 'C', 'F', 'G'};

    xs_reconfig_cmd_t cmd;
    int rc = xs_parse_reconfig(buf, sizeof(buf), &cmd);
    assert(rc != 0);
}

/* ---------- Next scene command ---------- */

static void test_serialize_next_scene_forward(void)
{
    uint8_t buf[16];
    size_t len = xs_serialize_next_scene(buf, sizeof(buf), 1);
    assert(len == 5);
    assert(buf[0] == 'N');
    assert(buf[1] == 'S');
    assert(buf[2] == 'C');
    assert(buf[3] == 'N');
    assert((int8_t)buf[4] == 1);
}

static void test_serialize_next_scene_backward(void)
{
    uint8_t buf[16];
    size_t len = xs_serialize_next_scene(buf, sizeof(buf), -1);
    assert(len == 5);
    assert((int8_t)buf[4] == -1);
}

static void test_parse_next_scene_roundtrip(void)
{
    uint8_t buf[16];
    size_t len = xs_serialize_next_scene(buf, sizeof(buf), 1);

    xs_next_scene_cmd_t cmd;
    int rc = xs_parse_next_scene(buf, len, &cmd);
    assert(rc == 0);
    assert(cmd.direction == 1);
}

static void test_parse_next_scene_backward(void)
{
    uint8_t buf[16];
    xs_serialize_next_scene(buf, sizeof(buf), -1);

    xs_next_scene_cmd_t cmd;
    assert(xs_parse_next_scene(buf, 5, &cmd) == 0);
    assert(cmd.direction == -1);
}

static void test_parse_next_scene_bad_magic(void)
{
    uint8_t buf[] = {'X', 'X', 'X', 'X', 1};
    xs_next_scene_cmd_t cmd;
    assert(xs_parse_next_scene(buf, sizeof(buf), &cmd) != 0);
}

static void test_parse_next_scene_truncated(void)
{
    uint8_t buf[] = {'N', 'S', 'C', 'N'};
    xs_next_scene_cmd_t cmd;
    assert(xs_parse_next_scene(buf, sizeof(buf), &cmd) != 0);
}

/* ---------- ACK response ---------- */

static void test_serialize_ack(void)
{
    uint8_t buf[16];
    size_t len = xs_serialize_ack(buf, sizeof(buf), 0);
    assert(len > 0);
    assert(buf[0] == 'A');
    assert(buf[1] == 'C');
    assert(buf[2] == 'K');
    assert(buf[3] == 0); /* success */
}

static void test_serialize_nack(void)
{
    uint8_t buf[16];
    size_t len = xs_serialize_ack(buf, sizeof(buf), 1);
    assert(len > 0);
    assert(buf[0] == 'A');
    assert(buf[1] == 'C');
    assert(buf[2] == 'K');
    assert(buf[3] == 1); /* error code */
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running control protocol tests...\n\n");

    printf("Serialize reconfig:\n");
    TEST(test_serialize_reconfig_cmd);
    TEST(test_serialize_reconfig_cmd_max_bands);

    printf("\nParse reconfig:\n");
    TEST(test_parse_reconfig_cmd);
    TEST(test_parse_reconfig_roundtrip_all_bands);
    TEST(test_parse_reconfig_bad_magic);
    TEST(test_parse_reconfig_truncated);

    printf("\nNext scene:\n");
    TEST(test_serialize_next_scene_forward);
    TEST(test_serialize_next_scene_backward);
    TEST(test_parse_next_scene_roundtrip);
    TEST(test_parse_next_scene_backward);
    TEST(test_parse_next_scene_bad_magic);
    TEST(test_parse_next_scene_truncated);

    printf("\nACK:\n");
    TEST(test_serialize_ack);
    TEST(test_serialize_nack);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
