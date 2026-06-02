#ifndef XSCAPE_CONTROL_H
#define XSCAPE_CONTROL_H

#include <stdint.h>
#include <stddef.h>
#include "../packets/packets.h"

/*
 * Control protocol between client and emulator.
 * Simple binary format over TCP control port.
 *
 * Reconfig command: RCFG magic + n_bands + wavelengths
 * ACK response:     ACK magic + status byte
 */

/* Command magics (for reference -- code checks individual bytes) */
/* RCFG = 0x47464352, ACK = 0x004B4341, NSCN = 0x4E43534E */

typedef struct {
    uint8_t  n_bands;
    uint16_t band_wavelengths[XS_MAX_BANDS];
} xs_reconfig_cmd_t;

/* Serialize a reconfig command. Returns bytes written or 0. */
size_t xs_serialize_reconfig(uint8_t *buf, size_t buf_size,
                              const xs_reconfig_cmd_t *cmd);

/* Parse a reconfig command. Returns 0 on success. */
int xs_parse_reconfig(const uint8_t *buf, size_t buf_len,
                      xs_reconfig_cmd_t *cmd);

/* Next scene command: NSCN magic + direction (int8: +1 or -1) */

typedef struct {
    int8_t direction; /* +1 = next, -1 = previous */
} xs_next_scene_cmd_t;

size_t xs_serialize_next_scene(uint8_t *buf, size_t buf_size, int8_t direction);
int xs_parse_next_scene(const uint8_t *buf, size_t buf_len, xs_next_scene_cmd_t *cmd);

/* Serialize an ACK response. status=0 for success. */
size_t xs_serialize_ack(uint8_t *buf, size_t buf_size, uint8_t status);

#endif /* XSCAPE_CONTROL_H */
