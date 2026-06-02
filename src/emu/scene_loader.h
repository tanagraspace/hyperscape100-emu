#ifndef XSCAPE_SCENE_LOADER_H
#define XSCAPE_SCENE_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "../packets/packets.h"

typedef struct {
    uint8_t  n_bands;
    uint16_t width;
    uint32_t n_lines;
    uint16_t wavelengths[XS_MAX_BANDS];
} xs_scene_t;

/* Load scene metadata from metadata.json in scene_dir. */
int xs_scene_load_metadata(const char *scene_dir, xs_scene_t *scene);

/* Load a single line (pixel-interleaved) from lines/line_NNNNN.bin.
 * line_buf must hold width * n_bands uint16 values. */
int xs_scene_load_line(const char *scene_dir, uint32_t line_num,
                       const xs_scene_t *scene, uint16_t *line_buf);

/* Load entire scene into memory. Caller must free *data. */
int xs_scene_load_all(const char *scene_dir, const xs_scene_t *scene,
                      uint16_t **data);

/* Extract one band from a pixel-interleaved line buffer. */
void xs_extract_band(const uint16_t *line_buf, uint16_t width,
                     uint8_t n_bands, uint8_t band_idx,
                     uint16_t *band_pixels);

#define XS_MAX_SCENES 256

typedef struct {
    char paths[XS_MAX_SCENES][512];
    int count;
    int current;
} xs_scene_list_t;

/* Scan a directory for scene subdirectories (containing metadata.json).
 * If path itself contains metadata.json, treats it as a single scene.
 * Returns 0 on success. */
int xs_scene_list_scan(const char *path, xs_scene_list_t *list);

/* Get current scene path. */
const char *xs_scene_list_current(const xs_scene_list_t *list);

/* Advance to next/previous scene. Wraps around. */
void xs_scene_list_advance(xs_scene_list_t *list, int direction);

#endif /* XSCAPE_SCENE_LOADER_H */
