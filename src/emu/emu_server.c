#include "emu_server.h"
#include "scene_loader.h"
#include "control.h"
#include "../packets/packets.h"
#include "../packets/serializer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PKT_BUF_SIZE 65536

static int create_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_pkt(int fd, const uint8_t *data, size_t len, int rate_mbps)
{
    size_t sent = 0;
    if (rate_mbps > 0) {
        double bytes_per_sec = (double)rate_mbps * 1000000.0 / 8.0;
        size_t chunk = 8192;
        int sleep_us = (int)((double)chunk / bytes_per_sec * 1000000.0);
        while (sent < len) {
            size_t to_send = (len - sent < chunk) ? len - sent : chunk;
            ssize_t n = send(fd, data + sent, to_send, 0);
            if (n <= 0) return -1;
            sent += n;
            if (sleep_us > 0) usleep(sleep_us);
        }
    } else {
        while (sent < len) {
            ssize_t n = send(fd, data + sent, len - sent, 0);
            if (n <= 0) return -1;
            sent += n;
        }
    }
    return 0;
}

static int stream_session(int data_fd, const char *scene_dir,
                           const xs_scene_t *scene,
                           uint8_t n_bands, const uint8_t *band_indices,
                           uint8_t source_n_bands,
                           const uint16_t *band_wavelengths,
                           uint32_t session_id, int rate_mbps)
{
    uint8_t *pkt = malloc(PKT_BUF_SIZE);
    if (!pkt) return -1;

    uint16_t *line_buf = malloc(scene->width * scene->n_bands * sizeof(uint16_t));
    uint16_t *band_pixels = malloc(scene->width * sizeof(uint16_t));
    if (!line_buf || !band_pixels) {
        free(pkt); free(line_buf); free(band_pixels);
        return -1;
    }

    uint8_t src_bands = source_n_bands > 0 ? source_n_bands : n_bands;
    size_t n;
    int rc = 0;

    /* SESSION START */
    n = xs_serialize_session_start(pkt, PKT_BUF_SIZE, 1, 0, 1, 1, session_id);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    /* IMAGER INFORMATION */
    n = xs_serialize_imager_info(pkt, PKT_BUF_SIZE, 0x000001, 1, 1, 0, 1, 0, 1);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    /* IMAGER CONFIGURATION */
    uint8_t band_setup[XS_MAX_BANDS];
    uint16_t band_start_rows[XS_MAX_BANDS];
    for (int i = 0; i < n_bands; i++) {
        band_setup[i] = 1;
        band_start_rows[i] = 0;
    }
    n = xs_serialize_imager_config(pkt, PKT_BUF_SIZE,
                                    XS_DEFAULT_LINE_PERIOD_US, n_bands,
                                    XS_DEFAULT_LINE_PERIOD_US,
                                    band_setup, 0, 0,
                                    band_start_rows, band_wavelengths, 0);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    /* TIME SYNC */
    n = xs_serialize_time_sync(pkt, PKT_BUF_SIZE, 0, 1, 0);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    /* SCENE START */
    n = xs_serialize_scene_start(pkt, PKT_BUF_SIZE, 0, XS_SCENE_LINE_SCAN,
                                  scene->n_lines, scene->width);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    uint64_t timestamp = 0;
    uint64_t last_telemetry = 0;
    size_t px_bytes = scene->width * 2; /* assumes XS_ENC_12BIT_16BIT (2 bytes/pixel) */

    for (uint32_t line = 0; line < scene->n_lines; line++) {
        /* Load one line from disk */
        if (xs_scene_load_line(scene_dir, line, scene, line_buf) != 0) {
            fprintf(stderr, "emu: failed to load line %u\n", line);
            rc = -1;
            goto done;
        }

        /* TELEMETRY every 500ms */
        if (timestamp - last_telemetry >= XS_TELEMETRY_INTERVAL_US) {
            n = xs_serialize_imager_telemetry(pkt, PKT_BUF_SIZE, timestamp, 25);
            if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }
            last_telemetry = timestamp;
        }

        /* EXPOSURE START */
        n = xs_serialize_exposure_start(pkt, PKT_BUF_SIZE, timestamp);
        if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

        /* LINE DATA per band */
        for (uint8_t b = 0; b < n_bands; b++) {
            uint8_t src_band = (src_bands > n_bands) ? band_indices[b] : b;
            xs_extract_band(line_buf, scene->width, src_bands, src_band, band_pixels);

            n = xs_serialize_line_data(pkt, PKT_BUF_SIZE,
                                        b, line, scene->width,
                                        XS_ENC_12BIT_16BIT,
                                        band_pixels, px_bytes);
            if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }
        }

        timestamp += XS_DEFAULT_LINE_PERIOD_US;

        if (line % 1000 == 0 && line > 0)
            fprintf(stderr, "emu: streamed %u/%u lines\n", line, scene->n_lines);
    }

    /* Final telemetry */
    n = xs_serialize_imager_telemetry(pkt, PKT_BUF_SIZE, timestamp, 25);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

    /* SESSION END */
    n = xs_serialize_session_end(pkt, PKT_BUF_SIZE, session_id);
    if (!n || send_pkt(data_fd, pkt, n, rate_mbps) < 0) { rc = -1; goto done; }

done:
    free(pkt);
    free(line_buf);
    free(band_pixels);
    return rc;
}

static void match_bands_to_indices(const xs_scene_t *scene,
                                    const uint16_t *req_wavelengths,
                                    uint8_t req_n_bands,
                                    uint8_t *band_indices,
                                    uint8_t *matched_count)
{
    *matched_count = 0;
    for (int i = 0; i < req_n_bands && *matched_count < XS_MAX_BANDS; i++) {
        int best = -1;
        int best_dist = 9999;
        for (int j = 0; j < scene->n_bands; j++) {
            int dist = abs((int)req_wavelengths[i] - (int)scene->wavelengths[j]);
            if (dist < best_dist) {
                best_dist = dist;
                best = j;
            }
        }
        if (best >= 0 && best_dist <= 5) {
            band_indices[*matched_count] = (uint8_t)best;
            (*matched_count)++;
        }
    }
}

static void reset_band_config(const xs_scene_t *scene,
                               uint8_t *n_bands, uint8_t *band_indices,
                               uint16_t *band_wavelengths, uint8_t *source_n_bands)
{
    *n_bands = scene->n_bands;
    *source_n_bands = 0;
    memcpy(band_wavelengths, scene->wavelengths, scene->n_bands * sizeof(uint16_t));
    for (int i = 0; i < scene->n_bands; i++) band_indices[i] = i;
}

int xs_emu_run(const xs_emu_cfg_t *cfg)
{
    xs_scene_list_t scenes;
    if (xs_scene_list_scan(cfg->scene_dir, &scenes) != 0) {
        fprintf(stderr, "emu: no scenes found in %s\n", cfg->scene_dir);
        return -1;
    }

    fprintf(stderr, "emu: found %d scene(s)\n", scenes.count);
    for (int i = 0; i < scenes.count; i++)
        fprintf(stderr, "  [%d] %s\n", i, scenes.paths[i]);

    xs_scene_t scene;
    if (xs_scene_load_metadata(xs_scene_list_current(&scenes), &scene) != 0) {
        fprintf(stderr, "emu: failed to load scene\n");
        return -1;
    }

    fprintf(stderr, "emu: active scene: %s (%u lines, %u px, %u bands)\n",
            xs_scene_list_current(&scenes), scene.n_lines, scene.width, scene.n_bands);

    uint8_t n_bands;
    uint8_t band_indices[XS_MAX_BANDS];
    uint16_t band_wavelengths[XS_MAX_BANDS];
    uint8_t source_n_bands;
    reset_band_config(&scene, &n_bands, band_indices, band_wavelengths, &source_n_bands);

    int data_listen = create_listen_socket(cfg->data_port);
    int ctrl_listen = create_listen_socket(cfg->ctrl_port);
    if (data_listen < 0 || ctrl_listen < 0) {
        fprintf(stderr, "emu: failed to create listen sockets\n");
        if (data_listen >= 0) close(data_listen);
        if (ctrl_listen >= 0) close(ctrl_listen);
        return -1;
    }

    uint32_t session_id = 1;

    int sessions_sent = 0;
    for (;;) {
        if (cfg->max_sessions > 0 && sessions_sent >= cfg->max_sessions)
            break;
        fprintf(stderr, "emu: waiting for connection on port %d (scene %d/%d)...\n",
                cfg->data_port, scenes.current + 1, scenes.count);

        int data_fd = accept(data_listen, NULL, NULL);
        if (data_fd < 0) break;

        fprintf(stderr, "emu: streaming session %u (%u bands)...\n",
                session_id, n_bands);

        int rc = stream_session(data_fd, xs_scene_list_current(&scenes), &scene,
                                 n_bands, band_indices, source_n_bands,
                                 band_wavelengths, session_id, cfg->rate_mbps);
        close(data_fd);

        if (rc == 0)
            fprintf(stderr, "emu: session %u complete\n", session_id);
        else
            fprintf(stderr, "emu: session %u ended early\n", session_id);

        sessions_sent++;
        session_id++;

        if (cfg->max_sessions > 0 && sessions_sent >= cfg->max_sessions)
            break;

        /* Wait for control command (timeout on accept, not recv) */
        struct timeval tv = {.tv_sec = 60, .tv_usec = 0};
        setsockopt(ctrl_listen, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int ctrl_fd = accept(ctrl_listen, NULL, NULL);
        if (ctrl_fd < 0) continue; /* timeout, re-stream same scene */

        uint8_t cmd_buf[256];
        ssize_t cmd_len = recv(ctrl_fd, cmd_buf, sizeof(cmd_buf), 0);
        uint8_t ack_buf[16];

        if (cmd_len >= 4) {
            xs_reconfig_cmd_t rcfg;
            xs_next_scene_cmd_t nscn;

            if (xs_parse_reconfig(cmd_buf, cmd_len, &rcfg) == 0) {
                uint8_t matched = 0;
                match_bands_to_indices(&scene, rcfg.band_wavelengths,
                                       rcfg.n_bands, band_indices, &matched);
                if (matched > 0) {
                    n_bands = matched;
                    source_n_bands = scene.n_bands;
                    for (int i = 0; i < matched; i++)
                        band_wavelengths[i] = scene.wavelengths[band_indices[i]];
                    fprintf(stderr, "emu: reconfigured to %u bands\n", n_bands);
                }
                size_t ack_len = xs_serialize_ack(ack_buf, sizeof(ack_buf), 0);
                send(ctrl_fd, ack_buf, ack_len, 0);

            } else if (xs_parse_next_scene(cmd_buf, cmd_len, &nscn) == 0) {
                xs_scene_list_advance(&scenes, nscn.direction);
                if (xs_scene_load_metadata(xs_scene_list_current(&scenes), &scene) == 0) {
                    reset_band_config(&scene, &n_bands, band_indices,
                                      band_wavelengths, &source_n_bands);
                    fprintf(stderr, "emu: switched to scene %d: %s (%u lines, %u bands)\n",
                            scenes.current + 1, xs_scene_list_current(&scenes),
                            scene.n_lines, scene.n_bands);
                    size_t ack_len = xs_serialize_ack(ack_buf, sizeof(ack_buf), 0);
                    send(ctrl_fd, ack_buf, ack_len, 0);
                } else {
                    fprintf(stderr, "emu: failed to load scene\n");
                    size_t ack_len = xs_serialize_ack(ack_buf, sizeof(ack_buf), 1);
                    send(ctrl_fd, ack_buf, ack_len, 0);
                }
            } else {
                size_t ack_len = xs_serialize_ack(ack_buf, sizeof(ack_buf), 1);
                send(ctrl_fd, ack_buf, ack_len, 0);
            }
        }
        close(ctrl_fd);
    }

    close(data_listen);
    close(ctrl_listen);
    return 0;
}
