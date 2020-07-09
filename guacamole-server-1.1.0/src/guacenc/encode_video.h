#ifndef __ENCODE_VIDEO_H
#define __ENCODE_VIDEO_H
#include "config.h"
#include "display.h"
#include "instructions.h"
#include "log.h"

#include <guacamole/client.h>
#include <guacamole/error.h>
#include <guacamole/parser.h>
#include <guacamole/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

//instructions to video
typedef struct InsTransVideo{
    guacenc_display* display;
    guac_parser* parser;
    char data[32768];
    int offsetLen;    
}InsTransVideo_t;

struct VideoConfig{
    int width;
    int height;
    int bitrate;
    char codec[20];
    char sid[256];
    char path[256];
    char outfile[256];
    char tmpfile[256];//m4v文件
    char recordingfile[256];
    char mode[20];
};
InsTransVideo_t*ins_transVideo_create(struct VideoConfig *config);

void ins_transVideo_delete(InsTransVideo_t *ins);

int ins_parser_read(InsTransVideo_t *ins);

int ins_read_video_configure(struct VideoConfig*config);

#endif

