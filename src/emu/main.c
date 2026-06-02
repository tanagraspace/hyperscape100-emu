#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_server.h"

int main(int argc, char *argv[])
{
    xs_emu_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.data_port = 4001;
    cfg.ctrl_port = 4002;
    cfg.rate_mbps = 0;
    cfg.max_sessions = 0; /* infinite by default */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            strncpy(cfg.scene_dir, argv[++i], sizeof(cfg.scene_dir) - 1);
        } else if (strcmp(argv[i], "--data-port") == 0 && i + 1 < argc) {
            cfg.data_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--control-port") == 0 && i + 1 < argc) {
            cfg.ctrl_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            cfg.rate_mbps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: emu --scene <path> [options]\n\n"
                   "  <path> can be a single scene directory or a parent directory\n"
                   "  containing multiple scenes. Scenes are directories with a\n"
                   "  metadata.json file and a lines/ subdirectory.\n\n"
                   "Options:\n"
                   "  --scene <path>        Scene path (required)\n"
                   "  --data-port <port>    Data stream port (default: 4001)\n"
                   "  --control-port <port> Control port (default: 4002)\n"
                   "  --rate <mbps>         Rate limit in Mbps (default: 0 = unlimited)\n");
            return 0;
        }
    }

    if (cfg.scene_dir[0] == '\0') {
        fprintf(stderr, "Error: --scene is required\n");
        return 1;
    }

    printf("HyperScape100 Emulator\n");
    printf("  Scene:   %s\n", cfg.scene_dir);
    printf("  Data:    port %d\n", cfg.data_port);
    printf("  Control: port %d\n", cfg.ctrl_port);
    printf("  Rate:    %s\n\n", cfg.rate_mbps > 0 ? "limited" : "unlimited");
    printf("Waiting for client connection...\n");

    int rc = xs_emu_run(&cfg);
    printf("Emulator stopped (rc=%d)\n", rc);
    return rc;
}
