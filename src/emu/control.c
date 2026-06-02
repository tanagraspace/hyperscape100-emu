#include "control.h"
#include <string.h>

/*
 * Reconfig command wire format:
 *   [0-3]  Magic "RCFG" (4 bytes)
 *   [4]    n_bands (uint8)
 *   [5..]  band_wavelengths (n_bands × uint16, little-endian)
 */

size_t xs_serialize_reconfig(uint8_t *buf, size_t buf_size,
                              const xs_reconfig_cmd_t *cmd)
{
    size_t needed = 4 + 1 + cmd->n_bands * 2;
    if (buf_size < needed) return 0;

    buf[0] = 'R';
    buf[1] = 'C';
    buf[2] = 'F';
    buf[3] = 'G';
    buf[4] = cmd->n_bands;
    memcpy(buf + 5, cmd->band_wavelengths, cmd->n_bands * sizeof(uint16_t));

    return needed;
}

int xs_parse_reconfig(const uint8_t *buf, size_t buf_len,
                      xs_reconfig_cmd_t *cmd)
{
    if (buf_len < 5) return -1;
    if (buf[0] != 'R' || buf[1] != 'C' || buf[2] != 'F' || buf[3] != 'G')
        return -1;

    cmd->n_bands = buf[4];
    if (cmd->n_bands > XS_MAX_BANDS) return -1;

    size_t needed = 5 + cmd->n_bands * 2;
    if (buf_len < needed) return -1;

    memset(cmd->band_wavelengths, 0, sizeof(cmd->band_wavelengths));
    memcpy(cmd->band_wavelengths, buf + 5, cmd->n_bands * sizeof(uint16_t));

    return 0;
}

/*
 * ACK wire format:
 *   [0-2]  "ACK" (3 bytes)
 *   [3]    status (uint8, 0 = success)
 */

/*
 * Next scene wire format:
 *   [0-3]  Magic "NSCN" (4 bytes)
 *   [4]    direction (int8: +1 = next, -1 = prev)
 */

size_t xs_serialize_next_scene(uint8_t *buf, size_t buf_size, int8_t direction)
{
    if (buf_size < 5) return 0;
    buf[0] = 'N'; buf[1] = 'S'; buf[2] = 'C'; buf[3] = 'N';
    buf[4] = (uint8_t)direction;
    return 5;
}

int xs_parse_next_scene(const uint8_t *buf, size_t buf_len, xs_next_scene_cmd_t *cmd)
{
    if (buf_len < 5) return -1;
    if (buf[0] != 'N' || buf[1] != 'S' || buf[2] != 'C' || buf[3] != 'N')
        return -1;
    cmd->direction = (int8_t)buf[4];
    return 0;
}

size_t xs_serialize_ack(uint8_t *buf, size_t buf_size, uint8_t status)
{
    if (buf_size < 4) return 0;

    buf[0] = 'A';
    buf[1] = 'C';
    buf[2] = 'K';
    buf[3] = status;

    return 4;
}
