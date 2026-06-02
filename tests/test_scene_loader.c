#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/packets/packets.h"
#include "../src/emu/scene_loader.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* Test fixture: create a minimal scene on disk */
static const char *TEST_DIR = "/tmp/hypercam_test_scene";

static void create_test_scene(int n_lines, int width, int n_bands,
                                const uint16_t *wavelengths)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/lines", TEST_DIR);
    mkdir(TEST_DIR, 0755);
    mkdir(path, 0755);

    /* Write metadata.json */
    snprintf(path, sizeof(path), "%s/metadata.json", TEST_DIR);
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "{\n");
    fprintf(f, "  \"n_bands\": %d,\n", n_bands);
    fprintf(f, "  \"width_px\": %d,\n", width);
    fprintf(f, "  \"n_lines\": %d,\n", n_lines);
    fprintf(f, "  \"dn_bits\": 12,\n");
    fprintf(f, "  \"wavelengths_nm\": [");
    for (int i = 0; i < n_bands; i++) {
        fprintf(f, "%d%s", wavelengths[i], i < n_bands - 1 ? ", " : "");
    }
    fprintf(f, "]\n}\n");
    fclose(f);

    /* Write line files: pixel-interleaved uint16 */
    for (int line = 0; line < n_lines; line++) {
        snprintf(path, sizeof(path), "%s/lines/line_%05d.bin", TEST_DIR, line);
        f = fopen(path, "wb");
        assert(f);
        for (int px = 0; px < width; px++) {
            for (int b = 0; b < n_bands; b++) {
                uint16_t val = (uint16_t)((line * 100 + px * 10 + b) & 0x0FFF);
                fwrite(&val, sizeof(uint16_t), 1, f);
            }
        }
        fclose(f);
    }
}

static void cleanup_test_scene(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

/* ---------- Metadata loading ---------- */

static void test_load_metadata(void)
{
    uint16_t wl[] = {500, 600, 700};
    create_test_scene(5, 8, 3, wl);

    xs_scene_t scene;
    int rc = xs_scene_load_metadata(TEST_DIR, &scene);
    assert(rc == 0);
    assert(scene.n_bands == 3);
    assert(scene.width == 8);
    assert(scene.n_lines == 5);
    assert(scene.wavelengths[0] == 500);
    assert(scene.wavelengths[1] == 600);
    assert(scene.wavelengths[2] == 700);

    cleanup_test_scene();
}

static void test_load_metadata_missing_dir(void)
{
    xs_scene_t scene;
    int rc = xs_scene_load_metadata("/tmp/nonexistent_scene_xyz", &scene);
    assert(rc != 0);
}

/* ---------- Line loading ---------- */

static void test_load_single_line(void)
{
    uint16_t wl[] = {500, 700};
    create_test_scene(3, 4, 2, wl);

    xs_scene_t scene;
    assert(xs_scene_load_metadata(TEST_DIR, &scene) == 0);

    uint16_t *line_buf = malloc(scene.width * scene.n_bands * sizeof(uint16_t));
    assert(line_buf);

    int rc = xs_scene_load_line(TEST_DIR, 0, &scene, line_buf);
    assert(rc == 0);

    /* line 0, px 0, band 0: (0*100 + 0*10 + 0) & 0xFFF = 0 */
    assert(line_buf[0 * scene.n_bands + 0] == 0);
    /* line 0, px 0, band 1: (0*100 + 0*10 + 1) & 0xFFF = 1 */
    assert(line_buf[0 * scene.n_bands + 1] == 1);
    /* line 0, px 1, band 0: (0*100 + 1*10 + 0) & 0xFFF = 10 */
    assert(line_buf[1 * scene.n_bands + 0] == 10);

    free(line_buf);
    cleanup_test_scene();
}

static void test_load_line_out_of_range(void)
{
    uint16_t wl[] = {500};
    create_test_scene(2, 4, 1, wl);

    xs_scene_t scene;
    assert(xs_scene_load_metadata(TEST_DIR, &scene) == 0);

    uint16_t *line_buf = malloc(scene.width * scene.n_bands * sizeof(uint16_t));
    int rc = xs_scene_load_line(TEST_DIR, 99, &scene, line_buf);
    assert(rc != 0);

    free(line_buf);
    cleanup_test_scene();
}

/* ---------- Full scene loading ---------- */

static void test_load_full_scene(void)
{
    uint16_t wl[] = {500, 600, 700};
    create_test_scene(3, 4, 3, wl);

    xs_scene_t scene;
    assert(xs_scene_load_metadata(TEST_DIR, &scene) == 0);

    uint16_t *data = NULL;
    int rc = xs_scene_load_all(TEST_DIR, &scene, &data);
    assert(rc == 0);
    assert(data != NULL);

    /* Verify a value from line 2, px 3, band 1 */
    /* (2*100 + 3*10 + 1) & 0xFFF = 231 */
    size_t idx = (2 * scene.width + 3) * scene.n_bands + 1;
    assert(data[idx] == 231);

    free(data);
    cleanup_test_scene();
}

/* ---------- Band extraction ---------- */

static void test_extract_band_from_line(void)
{
    uint16_t wl[] = {500, 600, 700};
    create_test_scene(1, 4, 3, wl);

    xs_scene_t scene;
    assert(xs_scene_load_metadata(TEST_DIR, &scene) == 0);

    uint16_t *line_buf = malloc(scene.width * scene.n_bands * sizeof(uint16_t));
    assert(xs_scene_load_line(TEST_DIR, 0, &scene, line_buf) == 0);

    uint16_t band_pixels[4];
    xs_extract_band(line_buf, scene.width, scene.n_bands, 1, band_pixels);

    /* band 1 values: px0=(0+0+1)=1, px1=(0+10+1)=11, px2=(0+20+1)=21, px3=(0+30+1)=31 */
    assert(band_pixels[0] == 1);
    assert(band_pixels[1] == 11);
    assert(band_pixels[2] == 21);
    assert(band_pixels[3] == 31);

    free(line_buf);
    cleanup_test_scene();
}

/* ---------- Scene list ---------- */

#define TEST_PARENT "/tmp/hs100_test_scenes"

static void create_multi_scene(void)
{
    char path[512];
    /* Create 3 scene subdirectories */
    for (int s = 0; s < 3; s++) {
        snprintf(path, sizeof(path), "%s/scene_%c/lines", TEST_PARENT, 'c' - s);
        mkdir(TEST_PARENT, 0755);
        snprintf(path, sizeof(path), "%s/scene_%c", TEST_PARENT, 'c' - s);
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/scene_%c/lines", TEST_PARENT, 'c' - s);
        mkdir(path, 0755);

        snprintf(path, sizeof(path), "%s/scene_%c/metadata.json", TEST_PARENT, 'c' - s);
        FILE *f = fopen(path, "w");
        fprintf(f, "{\"n_bands\":2,\"width_px\":4,\"n_lines\":1,\"wavelengths_nm\":[500,700]}\n");
        fclose(f);

        snprintf(path, sizeof(path), "%s/scene_%c/lines/line_00000.bin", TEST_PARENT, 'c' - s);
        f = fopen(path, "wb");
        uint16_t data[8] = {100,200,300,400,500,600,700,800};
        fwrite(data, sizeof(uint16_t), 8, f);
        fclose(f);
    }
}

static void cleanup_multi_scene(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_PARENT);
    system(cmd);
}

static void test_scene_list_scan_single(void)
{
    uint16_t wl[] = {500};
    create_test_scene(1, 4, 1, wl);

    xs_scene_list_t list;
    int rc = xs_scene_list_scan(TEST_DIR, &list);
    assert(rc == 0);
    assert(list.count == 1);
    assert(list.current == 0);
    assert(strcmp(xs_scene_list_current(&list), TEST_DIR) == 0);

    cleanup_test_scene();
}

static void test_scene_list_scan_multiple(void)
{
    create_multi_scene();

    xs_scene_list_t list;
    int rc = xs_scene_list_scan(TEST_PARENT, &list);
    assert(rc == 0);
    assert(list.count == 3);
    assert(list.current == 0);

    /* Should be sorted alphabetically */
    assert(strstr(xs_scene_list_current(&list), "scene_a") != NULL);

    cleanup_multi_scene();
}

static void test_scene_list_scan_empty(void)
{
    mkdir("/tmp/hs100_empty_dir", 0755);
    xs_scene_list_t list;
    assert(xs_scene_list_scan("/tmp/hs100_empty_dir", &list) != 0);
    rmdir("/tmp/hs100_empty_dir");
}

static void test_scene_list_advance_forward(void)
{
    create_multi_scene();

    xs_scene_list_t list;
    xs_scene_list_scan(TEST_PARENT, &list);
    assert(list.current == 0);

    xs_scene_list_advance(&list, 1);
    assert(list.current == 1);

    xs_scene_list_advance(&list, 1);
    assert(list.current == 2);

    /* Wraps around */
    xs_scene_list_advance(&list, 1);
    assert(list.current == 0);

    cleanup_multi_scene();
}

static void test_scene_list_advance_backward(void)
{
    create_multi_scene();

    xs_scene_list_t list;
    xs_scene_list_scan(TEST_PARENT, &list);
    assert(list.current == 0);

    /* Wraps backward */
    xs_scene_list_advance(&list, -1);
    assert(list.current == 2);

    xs_scene_list_advance(&list, -1);
    assert(list.current == 1);

    cleanup_multi_scene();
}

static void test_scene_list_advance_single(void)
{
    uint16_t wl[] = {500};
    create_test_scene(1, 4, 1, wl);

    xs_scene_list_t list;
    xs_scene_list_scan(TEST_DIR, &list);
    assert(list.count == 1);

    /* Should not move with only one scene */
    xs_scene_list_advance(&list, 1);
    assert(list.current == 0);

    xs_scene_list_advance(&list, -1);
    assert(list.current == 0);

    cleanup_test_scene();
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running scene loader tests...\n\n");

    printf("Metadata:\n");
    TEST(test_load_metadata);
    TEST(test_load_metadata_missing_dir);

    printf("\nLine loading:\n");
    TEST(test_load_single_line);
    TEST(test_load_line_out_of_range);

    printf("\nFull scene:\n");
    TEST(test_load_full_scene);

    printf("\nBand extraction:\n");
    TEST(test_extract_band_from_line);

    printf("\nScene list:\n");
    TEST(test_scene_list_scan_single);
    TEST(test_scene_list_scan_multiple);
    TEST(test_scene_list_scan_empty);
    TEST(test_scene_list_advance_forward);
    TEST(test_scene_list_advance_backward);
    TEST(test_scene_list_advance_single);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
