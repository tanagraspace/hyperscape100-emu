#include "session.h"
#include "serializer.h"
#include <string.h>
#include <stdlib.h>

/* Uses XS_DEFAULT_LINE_PERIOD_US and XS_TELEMETRY_INTERVAL_US from packets.h */

static size_t pixel_bytes_per_line(const xs_session_cfg_t *cfg)
{
    switch (cfg->encoding) {
        case XS_ENC_8BIT:       return cfg->width * 1;
        case XS_ENC_12BIT_16BIT:
        case XS_ENC_10BIT_16BIT:
        case XS_ENC_16BIT:      return cfg->width * 2;
        default:                return cfg->width * 2;
    }
}

static size_t line_data_pkt_size(const xs_session_cfg_t *cfg)
{
    size_t px_bytes = pixel_bytes_per_line(cfg);
    size_t payload = sizeof(xs_line_data_hdr_t) + px_bytes;
    size_t padded = (payload + 3) & ~(size_t)3;
    return XS_HEADER_SIZE + padded + XS_FOOTER_SIZE;
}

static uint32_t get_line_period(const xs_session_cfg_t *cfg)
{
    return cfg->line_period_us > 0 ? cfg->line_period_us : XS_DEFAULT_LINE_PERIOD_US;
}

size_t xs_session_size(const xs_session_cfg_t *cfg)
{
    size_t session_start  = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    size_t imager_info    = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    size_t time_sync      = XS_HEADER_SIZE + 20 + XS_FOOTER_SIZE;
    size_t scene_start    = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    size_t session_end    = XS_HEADER_SIZE + 4  + XS_FOOTER_SIZE;
    size_t exp_start      = XS_HEADER_SIZE + 8  + XS_FOOTER_SIZE;
    size_t telemetry      = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;

    size_t ic_payload = 12 + cfg->n_bands + 2 + cfg->n_bands * 4 + 1;
    size_t ic_padded = (ic_payload + 3) & ~(size_t)3;
    size_t imager_config = XS_HEADER_SIZE + ic_padded + XS_FOOTER_SIZE;

    size_t per_line = exp_start + cfg->n_bands * line_data_pkt_size(cfg);

    uint32_t lp = get_line_period(cfg);
    uint64_t total_time_us = (uint64_t)cfg->n_lines * lp;
    size_t n_telemetry = (size_t)(total_time_us / XS_TELEMETRY_INTERVAL_US) + 2;

    return session_start + imager_info + imager_config + time_sync
         + scene_start
         + cfg->n_lines * per_line
         + n_telemetry * telemetry
         + session_end;
}

size_t xs_build_session(uint8_t *buf, size_t buf_size,
                        const xs_session_cfg_t *cfg,
                        const uint16_t *scene_data)
{
    size_t needed = xs_session_size(cfg);
    if (!buf || buf_size < needed) return 0;

    uint8_t src_bands = cfg->source_n_bands;
    if (src_bands == 0) src_bands = cfg->n_bands;

    uint32_t lp = get_line_period(cfg);

    size_t pos = 0;
    size_t n;

    /* 1. SESSION START (ICD 4.3.1: "always the first packet") */
    n = xs_serialize_session_start(buf + pos, buf_size - pos,
                                    1, 0,
                                    cfg->platform_id,
                                    cfg->instrument_id,
                                    cfg->session_id);
    if (n == 0) return 0;
    pos += n;

    /* 2. IMAGER INFORMATION (ICD 4.3.10.1: "inserted automatically at the start of a session") */
    n = xs_serialize_imager_info(buf + pos, buf_size - pos,
                                 0x000001, 1,
                                 1, 0, 1, 0, 1);
    if (n == 0) return 0;
    pos += n;

    /* 3. IMAGER CONFIGURATION (ICD 4.3.10.2: "inserted automatically once configured, before image") */
    uint8_t band_setup[XS_MAX_BANDS];
    uint16_t band_start_rows[XS_MAX_BANDS];
    for (int i = 0; i < cfg->n_bands; i++) {
        band_setup[i] = 1;
        band_start_rows[i] = 0;
    }

    n = xs_serialize_imager_config(buf + pos, buf_size - pos,
                                    lp, cfg->n_bands, lp,
                                    band_setup, 0, 0,
                                    band_start_rows,
                                    cfg->band_wavelengths, 0);
    if (n == 0) return 0;
    pos += n;

    /* 4. TIME SYNCHRONISATION (ICD 4.3.7) */
    n = xs_serialize_time_sync(buf + pos, buf_size - pos,
                                0, 1, 0);
    if (n == 0) return 0;
    pos += n;

    /* 5. SCENE START (ICD 4.3.3: "always precedes any Line Data packets") */
    n = xs_serialize_scene_start(buf + pos, buf_size - pos,
                                  0, XS_SCENE_LINE_SCAN,
                                  cfg->n_lines, cfg->width);
    if (n == 0) return 0;
    pos += n;

    /* Per-line pixel buffer */
    size_t px_bytes = pixel_bytes_per_line(cfg);
    uint16_t *band_pixels = malloc(cfg->width * sizeof(uint16_t));
    if (!band_pixels) return 0;

    uint64_t timestamp = 0;
    uint64_t last_telemetry_time = 0;

    for (uint32_t line = 0; line < cfg->n_lines; line++) {
        /* IMAGER TELEMETRY (ICD 4.3.10.4: "inserted every 500 ms") */
        if (timestamp - last_telemetry_time >= XS_TELEMETRY_INTERVAL_US) {
            n = xs_serialize_imager_telemetry(buf + pos, buf_size - pos,
                                               timestamp, 25);
            if (n == 0) { free(band_pixels); return 0; }
            pos += n;
            last_telemetry_time = timestamp;
        }

        /* EXPOSURE START (ICD 4.3.4: "always occurs before the Line Data packets") */
        n = xs_serialize_exposure_start(buf + pos, buf_size - pos, timestamp);
        if (n == 0) { free(band_pixels); return 0; }
        pos += n;

        /* LINE DATA (ICD 4.3.5: one packet per band per line) */
        for (uint8_t b = 0; b < cfg->n_bands; b++) {
            uint8_t src_band = (src_bands > cfg->n_bands) ? cfg->band_indices[b] : b;

            for (uint16_t px = 0; px < cfg->width; px++) {
                size_t idx = ((size_t)line * cfg->width + px) * src_bands + src_band;
                band_pixels[px] = scene_data[idx];
            }

            n = xs_serialize_line_data(buf + pos, buf_size - pos,
                                        b, line, cfg->width,
                                        cfg->encoding,
                                        band_pixels, px_bytes);
            if (n == 0) { free(band_pixels); return 0; }
            pos += n;
        }

        timestamp += lp;
    }

    free(band_pixels);

    /* Final telemetry */
    n = xs_serialize_imager_telemetry(buf + pos, buf_size - pos,
                                       timestamp, 25);
    if (n == 0) return 0;
    pos += n;

    /* SESSION END (ICD 4.3.2: "always the last packet") */
    n = xs_serialize_session_end(buf + pos, buf_size - pos, cfg->session_id);
    if (n == 0) return 0;
    pos += n;

    return pos;
}
