#include "scene_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal JSON parser -- just extracts the fields we need from metadata.json.
 * Not a general JSON parser. Handles our known format only. */
static int parse_int_field(const char *json, const char *field, int *val)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    *val = atoi(p);
    return 0;
}

static int parse_int_array(const char *json, const char *field,
                           uint16_t *arr, int max_len, int *count)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    p++;

    *count = 0;
    while (*p && *p != ']' && *count < max_len) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']') break;
        arr[*count] = (uint16_t)atoi(p);
        (*count)++;
        while (*p && *p != ',' && *p != ']') p++;
    }
    return 0;
}

int xs_scene_load_metadata(const char *scene_dir, xs_scene_t *scene)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/metadata.json", scene_dir);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json = malloc(fsize + 1);
    if (!json) { fclose(f); return -1; }
    if (fread(json, 1, fsize, f) != (size_t)fsize) {
        free(json); fclose(f); return -1;
    }
    json[fsize] = '\0';
    fclose(f);

    memset(scene, 0, sizeof(*scene));

    int val;
    if (parse_int_field(json, "n_bands", &val) == 0) scene->n_bands = (uint8_t)val;
    if (parse_int_field(json, "width_px", &val) == 0) scene->width = (uint16_t)val;
    if (parse_int_field(json, "n_lines", &val) == 0) scene->n_lines = (uint32_t)val;

    int wl_count = 0;
    parse_int_array(json, "wavelengths_nm", scene->wavelengths, XS_MAX_BANDS, &wl_count);

    free(json);

    if (scene->n_bands == 0 || scene->width == 0 || scene->n_lines == 0)
        return -1;

    return 0;
}

int xs_scene_load_line(const char *scene_dir, uint32_t line_num,
                       const xs_scene_t *scene, uint16_t *line_buf)
{
    if (line_num >= scene->n_lines) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/lines/line_%05u.bin", scene_dir, line_num);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t expected = (size_t)scene->width * scene->n_bands;
    size_t read = fread(line_buf, sizeof(uint16_t), expected, f);
    fclose(f);

    return (read == expected) ? 0 : -1;
}

int xs_scene_load_all(const char *scene_dir, const xs_scene_t *scene,
                      uint16_t **data)
{
    size_t total = (size_t)scene->n_lines * scene->width * scene->n_bands;
    *data = malloc(total * sizeof(uint16_t));
    if (!*data) return -1;

    for (uint32_t line = 0; line < scene->n_lines; line++) {
        uint16_t *line_ptr = *data + (size_t)line * scene->width * scene->n_bands;
        if (xs_scene_load_line(scene_dir, line, scene, line_ptr) != 0) {
            free(*data);
            *data = NULL;
            return -1;
        }
    }
    return 0;
}

void xs_extract_band(const uint16_t *line_buf, uint16_t width,
                     uint8_t n_bands, uint8_t band_idx,
                     uint16_t *band_pixels)
{
    for (uint16_t px = 0; px < width; px++) {
        band_pixels[px] = line_buf[(size_t)px * n_bands + band_idx];
    }
}

#include <dirent.h>
#include <sys/stat.h>

static int has_metadata(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/metadata.json", dir);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int cmp_strings(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

int xs_scene_list_scan(const char *path, xs_scene_list_t *list)
{
    memset(list, 0, sizeof(*list));

    /* Check if path itself is a scene */
    if (has_metadata(path)) {
        strncpy(list->paths[0], path, sizeof(list->paths[0]) - 1);
        list->count = 1;
        list->current = 0;
        return 0;
    }

    /* Scan subdirectories */
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && list->count < XS_MAX_SCENES) {
        if (ent->d_name[0] == '.') continue;

        char subdir[512];
        snprintf(subdir, sizeof(subdir), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(subdir, &st) == 0 && S_ISDIR(st.st_mode) && has_metadata(subdir)) {
            strncpy(list->paths[list->count], subdir,
                    sizeof(list->paths[0]) - 1);
            list->count++;
        }
    }
    closedir(d);

    if (list->count == 0) return -1;

    /* Sort for consistent ordering */
    char *ptrs[XS_MAX_SCENES];
    for (int i = 0; i < list->count; i++) ptrs[i] = list->paths[i];
    qsort(ptrs, list->count, sizeof(char *), cmp_strings);

    /* Copy sorted order back */
    char sorted[XS_MAX_SCENES][512];
    for (int i = 0; i < list->count; i++)
        strncpy(sorted[i], ptrs[i], sizeof(sorted[0]) - 1);
    memcpy(list->paths, sorted, sizeof(list->paths));

    list->current = 0;
    return 0;
}

const char *xs_scene_list_current(const xs_scene_list_t *list)
{
    if (list->count == 0) return NULL;
    return list->paths[list->current];
}

void xs_scene_list_advance(xs_scene_list_t *list, int direction)
{
    if (list->count <= 1) return;
    list->current = (list->current + direction + list->count) % list->count;
}
