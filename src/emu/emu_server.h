#ifndef XSCAPE_EMU_SERVER_H
#define XSCAPE_EMU_SERVER_H

#include <stdint.h>

typedef struct {
    char     scene_dir[512];
    int      data_port;
    int      ctrl_port;
    int      rate_mbps;     /* 0 = unlimited */
    int      max_sessions;  /* 0 = infinite, >0 = stop after N sessions */
} xs_emu_cfg_t;

/*
 * Run the emulator server. Blocks until max_sessions reached or error.
 * Returns 0 on clean exit.
 */
int xs_emu_run(const xs_emu_cfg_t *cfg);

#endif /* XSCAPE_EMU_SERVER_H */
