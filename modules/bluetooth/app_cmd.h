#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>

#define APP_MAX_FILENAME 32

enum app_cmd_type {
    APP_CMD_PLAY = 1,
    APP_CMD_STOP,
    APP_CMD_PAUSE,
    APP_CMD_RESUME,
    APP_CMD_SET_VOLUME,
    APP_CMD_ADS_START,
    APP_CMD_ADS_STOP,
};

struct app_cmd {
    enum app_cmd_type type;
    uint8_t volume;
    char filename[APP_MAX_FILENAME];
};

#endif
