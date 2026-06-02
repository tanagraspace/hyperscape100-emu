#ifndef XSCAPE_SESSION_H
#define XSCAPE_SESSION_H

#include <stdint.h>
#include <stddef.h>
#include "packets.h"

/*
 * Session configuration for the emulator.
 */
typedef struct {
    uint32_t session_id;
    uint16_t platform_id;
    uint16_t instrument_id;
    uint8_t  n_bands;
    uint32_t n_lines;
    uint16_t width;
    uint16_t band_wavelengths[XS_MAX_BANDS];
    uint8_t  band_indices[XS_MAX_BANDS]; /* source band indices for band selection */
    uint8_t  source_n_bands;             /* 0 = same as n_bands (no remapping) */
    uint8_t  encoding;
    uint32_t line_period_us;             /* microseconds per line (0 = default 1000) */
} xs_session_cfg_t;

/*
 * Estimate the buffer size needed for a complete session.
 */
size_t xs_session_size(const xs_session_cfg_t *cfg);

/*
 * Build a complete session binary from pixel data.
 *
 * scene_data is pixel-interleaved: for each line, for each pixel,
 * all band values are contiguous. Layout:
 *   scene_data[(line * width + px) * source_n_bands + band]
 *
 * If source_n_bands == 0 or source_n_bands == n_bands, all bands
 * are used in order. If source_n_bands > n_bands, band_indices
 * selects which source bands to include.
 *
 * Returns bytes written, or 0 on error.
 */
size_t xs_build_session(uint8_t *buf, size_t buf_size,
                        const xs_session_cfg_t *cfg,
                        const uint16_t *scene_data);

#endif /* XSCAPE_SESSION_H */
