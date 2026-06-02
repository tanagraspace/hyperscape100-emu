#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "../src/packets/packets.h"
#include "../src/packets/crc32.h"
#include "../src/packets/pkt_parser.h"
#include "../src/emu/control.h"
#include "../src/emu/emu_server.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define DATA_PORT    14001
#define CTRL_PORT    14002
#define TEST_DIR     "/tmp/hypercam_test_emu"

/* ---------- Helpers ---------- */

static void create_test_scene(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/lines", TEST_DIR);
    mkdir(TEST_DIR, 0755);
    mkdir(path, 0755);

    /* 3 bands, 8 pixels wide, 4 lines */
    uint16_t wl[] = {500, 600, 700};
    int n_bands = 3, width = 8, n_lines = 4;

    snprintf(path, sizeof(path), "%s/metadata.json", TEST_DIR);
    FILE *f = fopen(path, "w");
    fprintf(f, "{\"n_bands\":%d,\"width_px\":%d,\"n_lines\":%d,"
               "\"wavelengths_nm\":[%d,%d,%d]}\n",
            n_bands, width, n_lines, wl[0], wl[1], wl[2]);
    fclose(f);

    for (int line = 0; line < n_lines; line++) {
        snprintf(path, sizeof(path), "%s/lines/line_%05d.bin", TEST_DIR, line);
        f = fopen(path, "wb");
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

static int tcp_connect(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0) { close(fd); return -1; }
    return fd;
}

static size_t recv_all(int fd, uint8_t *buf, size_t max_len, int timeout_ms)
{
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t total = 0;
    while (total < max_len) {
        ssize_t n = recv(fd, buf + total, max_len - total, 0);
        if (n <= 0) break;
        total += n;
    }
    return total;
}

/* Count packets in a received stream */
typedef struct {
    int session_start;
    int session_end;
    int scene_start;
    int exposure_start;
    int line_data;
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
            case XS_PKT_SESSION_START:  c.session_start++; break;
            case XS_PKT_SESSION_END:    c.session_end++; break;
            case XS_PKT_SCENE_START:    c.scene_start++; break;
            case XS_PKT_EXPOSURE_START: c.exposure_start++; break;
            case XS_PKT_LINE_DATA:      c.line_data++; break;
        }
        c.total++;
        pos += hdr.total_len;
    }
    return c;
}

/* Emulator server thread */
static xs_emu_cfg_t server_cfg;
static pthread_t server_thread;

static void *server_run(void *arg)
{
    (void)arg;
    xs_emu_run(&server_cfg);
    return NULL;
}

static void start_server(int max_sessions)
{
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.scene_dir, TEST_DIR, sizeof(server_cfg.scene_dir) - 1);
    server_cfg.data_port = DATA_PORT;
    server_cfg.ctrl_port = CTRL_PORT;
    server_cfg.rate_mbps = 0; /* unlimited */
    server_cfg.max_sessions = max_sessions;

    pthread_create(&server_thread, NULL, server_run, NULL);
    usleep(100000); /* wait for server to start */
}

static void stop_server(void)
{
    pthread_join(server_thread, NULL);
}

/* ---------- Tests ---------- */

static void test_emu_streams_valid_session(void)
{
    create_test_scene();
    start_server(1);

    int fd = tcp_connect(DATA_PORT);
    assert(fd >= 0);

    /* Scene: 4 lines × 8 px × 3 bands, session should be small */
    uint8_t *buf = malloc(1024 * 1024);
    size_t received = recv_all(fd, buf, 1024 * 1024, 2000);
    close(fd);

    assert(received > 0);

    pkt_counts_t c = count_packets(buf, received);
    assert(c.session_start == 1);
    assert(c.session_end == 1);
    assert(c.scene_start == 1);
    assert(c.exposure_start == 4); /* 4 lines */
    assert(c.line_data == 12);     /* 4 lines × 3 bands */
    assert(c.crc_errors == 0);

    free(buf);
    stop_server();
    cleanup_test_scene();
}

static void test_emu_reconfig_changes_bands(void)
{
    create_test_scene();
    start_server(2); /* allow 2 sessions */

    /* Connect data + control */
    int data_fd = tcp_connect(DATA_PORT);
    assert(data_fd >= 0);

    /* Receive first session (3 bands) */
    uint8_t *buf = malloc(1024 * 1024);
    size_t received = recv_all(data_fd, buf, 1024 * 1024, 2000);
    assert(received > 0);

    pkt_counts_t c1 = count_packets(buf, received);
    assert(c1.line_data == 12); /* 4 lines × 3 bands */
    close(data_fd);

    /* Send reconfig: reduce to 2 bands */
    int ctrl_fd = tcp_connect(CTRL_PORT);
    assert(ctrl_fd >= 0);

    xs_reconfig_cmd_t cmd = {
        .n_bands = 2,
        .band_wavelengths = {500, 700},
    };
    uint8_t cmd_buf[128];
    size_t cmd_len = xs_serialize_reconfig(cmd_buf, sizeof(cmd_buf), &cmd);
    send(ctrl_fd, cmd_buf, cmd_len, 0);

    /* Receive ACK */
    uint8_t ack_buf[16];
    size_t ack_len = recv_all(ctrl_fd, ack_buf, sizeof(ack_buf), 2000);
    assert(ack_len >= 4);
    assert(ack_buf[0] == 'A' && ack_buf[1] == 'C' && ack_buf[2] == 'K');
    assert(ack_buf[3] == 0); /* success */
    close(ctrl_fd);

    /* Receive second session (2 bands) */
    data_fd = tcp_connect(DATA_PORT);
    assert(data_fd >= 0);

    received = recv_all(data_fd, buf, 1024 * 1024, 2000);
    assert(received > 0);

    pkt_counts_t c2 = count_packets(buf, received);
    assert(c2.line_data == 8); /* 4 lines × 2 bands */
    assert(c2.crc_errors == 0);
    close(data_fd);

    free(buf);
    stop_server();
    cleanup_test_scene();
}

static void test_emu_pixel_data_integrity(void)
{
    create_test_scene();
    start_server(1);

    int fd = tcp_connect(DATA_PORT);
    assert(fd >= 0);

    uint8_t *buf = malloc(1024 * 1024);
    size_t received = recv_all(fd, buf, 1024 * 1024, 2000);
    close(fd);

    /* Find first LINE DATA packet for band 0, line 0 and verify pixels */
    size_t pos = 0;
    int found = 0;
    while (pos < received) {
        xs_parsed_header_t hdr;
        if (xs_parse_header(buf + pos, received - pos, &hdr) != 0) break;
        if (pos + hdr.total_len > received) break;

        if (hdr.id == XS_PKT_LINE_DATA) {
            xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + pos + XS_HEADER_SIZE);
            if (lh->spectral_band == 0 && xs_decode_u24(lh->line_number) == 0) {
                uint16_t *px = (uint16_t *)(buf + pos + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t));
                /* line 0, px 0, band 0: (0*100 + 0*10 + 0) = 0 */
                assert(px[0] == 0);
                /* line 0, px 1, band 0: (0*100 + 1*10 + 0) = 10 */
                assert(px[1] == 10);
                /* line 0, px 7, band 0: (0*100 + 7*10 + 0) = 70 */
                assert(px[7] == 70);
                found = 1;
                break;
            }
        }
        pos += hdr.total_len;
    }
    assert(found);

    free(buf);
    stop_server();
    cleanup_test_scene();
}

/* ---------- Main ---------- */

int main(void)
{
    printf("Running emulator TCP tests...\n\n");

    printf("Streaming:\n");
    TEST(test_emu_streams_valid_session);

    printf("\nReconfig:\n");
    TEST(test_emu_reconfig_changes_bands);

    printf("\nData integrity:\n");
    TEST(test_emu_pixel_data_integrity);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
